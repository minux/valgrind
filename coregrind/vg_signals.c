
/*--------------------------------------------------------------------*/
/*--- Implementation of POSIX signals.                             ---*/
/*---                                                 vg_signals.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, an extensible x86 protected-mode
   emulator for monitoring program execution on x86-Unixes.

   Copyright (C) 2000-2004 Julian Seward 
      jseward@acm.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

/* 
   New signal handling.

   Now that all threads have a ProxyLWP to deal with signals for them,
   we can use the kernel to do a lot more work for us.  The kernel
   will deal with blocking signals, pending blocked signals, queues
   and thread selection.  We just need to deal with setting a signal
   handler and signal delivery.

   In order to match the proper kernel signal semantics, the proxy LWP
   which recieves a signal goes through an exchange of messages with
   the scheduler LWP.  When the proxy first gets a signal, it
   immediately blocks all signals and sends a message back to the
   scheduler LWP.  It then enters a SigACK state, in which requests to
   run system calls are ignored, and all signals remain blocked.  When
   the scheduler gets the signal message, it sets up the thread to
   enter its signal handler, and sends a SigACK message back to the
   proxy, which includes the signal mask to be applied while running
   the handler.  On recieving SigACK, the proxy sets the new signal
   mask and reverts to its normal mode of operation. (All this is
   implemented in vg_syscalls.c)

   This protocol allows the application thread to take delivery of the
   signal at some arbitary time after the signal was sent to the
   process, while still getting proper signal delivery semantics (most
   notably, getting the signal block sets right while running the
   signal handler, and not allowing recursion where there wouldn't
   have been normally).

   Important point: the main LWP *always* has all signals blocked
   except for SIGSEGV, SIGBUS, SIGFPE and SIGILL (ie, signals which
   are synchronously changed .  If the kernel supports thread groups
   with shared signal state (Linux 2.5+, RedHat's 2.4), then these are
   the only signals it needs to handle.

   If we get a synchronous signal, we longjmp back into the scheduler,
   since we can't resume executing the client code.  The scheduler
   immediately starts signal delivery to the thread which generated
   the signal.

   On older kernels without thread-groups, we need to poll the pending
   signal with sigtimedwait() and farm any signals off to the
   appropriate proxy LWP.
 */

#include "vg_include.h"
#include <stddef.h>		/* OK, no library dependencies */

/* Define to give more sanity checking for signals. */
#define DEBUG_SIGNALS


/*
   - The following causes an infinite loop: start Hugs, Feb 2001 
     version, and do Control-C at the prompt.  There is an infinite
     series of sigints delivered (to the client); but also seemingly
     to valgrind, which is very strange.  I don't know why.

     [I haven't re-tested this, but this is likely fixed - JSGF]
*/


/* ---------------------------------------------------------------------
   Forwards decls.
   ------------------------------------------------------------------ */

static void vg_sync_signalhandler  ( Int sigNo, vki_ksiginfo_t *info, struct vki_ucontext * );
static void vg_async_signalhandler ( Int sigNo, vki_ksiginfo_t *info, struct vki_ucontext * );
static void vg_babyeater	   ( Int sigNo, vki_ksiginfo_t *info, struct vki_ucontext * );
static void proxy_sigvg_handler	   ( Int sigNo, vki_ksiginfo_t *info, struct vki_ucontext * );

static Bool is_correct_sigmask(void);
static const Char *signame(Int sigNo);

/* ---------------------------------------------------------------------
   HIGH LEVEL STUFF TO DO WITH SIGNALS: POLICY (MOSTLY)
   ------------------------------------------------------------------ */

/* If set to true, the currently running kernel doesn't do the right
   thing with signals and LWPs, so we need to do our own. */
Bool VG_(do_signal_routing) = False;

/* Set of signal which are pending for the whole process.  This is
   only used when we're doing signal routing, and this is a place to
   remember pending signals which we can't keep actually pending for
   some reason. */
static vki_ksigset_t proc_pending; /* process-wide pending signals */

/* Since we use a couple of RT signals, we need to handle allocating
   the rest for application use. */
Int VG_(sig_rtmin) = VKI_SIGRTUSERMIN;
Int VG_(sig_rtmax) = VKI_SIGRTMAX;

Int VG_(sig_alloc_rtsig)(Int high)
{
   Int ret;

   if (VG_(sig_rtmin) >= VG_(sig_rtmax))
      ret = -1;
   else
      ret = high ? VG_(sig_rtmin)++ : VG_(sig_rtmax)--;

   vg_assert(ret >= VKI_SIGRTUSERMIN);

   return ret;
}

/* ---------------------------------------------------------------------
   Signal state for this process.
   ------------------------------------------------------------------ */


/* Base-ment of these arrays[VKI_KNSIG].

   Valid signal numbers are 1 .. VKI_KNSIG inclusive.
   Rather than subtracting 1 for indexing these arrays, which
   is tedious and error-prone, they are simply dimensioned 1 larger,
   and entry [0] is not used. 
 */


/* -----------------------------------------------------
   Static client signal state (SCSS).  This is the state
   that the client thinks it has the kernel in.  
   SCSS records verbatim the client's settings.  These 
   are mashed around only when SKSS is calculated from it.
   -------------------------------------------------- */

typedef 
   struct {
      void* scss_handler;  /* VKI_SIG_DFL or VKI_SIG_IGN or ptr to
                              client's handler */
      UInt  scss_flags;
      vki_ksigset_t scss_mask;
      void* scss_restorer; /* god knows; we ignore it. */
   }
   SCSS_Per_Signal;

typedef 
   struct {
      /* per-signal info */
      SCSS_Per_Signal scss_per_sig[1+VKI_KNSIG];

      /* Additional elements to SCSS not stored here:
         - for each thread, the thread's blocking mask
         - for each thread in WaitSIG, the set of waited-on sigs
      */
      } 
      SCSS;

static SCSS vg_scss;


/* -----------------------------------------------------
   Static kernel signal state (SKSS).  This is the state
   that we have the kernel in.  It is computed from SCSS.
   -------------------------------------------------- */

/* Let's do: 
     sigprocmask assigns to all thread masks
     so that at least everything is always consistent
   Flags:
     SA_SIGINFO -- we always set it, and honour it for the client
     SA_NOCLDSTOP -- passed to kernel
     SA_ONESHOT or SA_RESETHAND -- required; abort if not set
     SA_RESTART -- we observe this but set our handlers to always restart
     SA_NOMASK or SA_NODEFER -- we observe this, but our handlers block everything
     SA_ONSTACK -- currently not supported; abort if set.
     SA_NOCLDWAIT -- we observe this, but we never set it (doesn't quite 
	work if client is blocked in a wait4() syscall)
*/


typedef 
   struct {
      void* skss_handler;  /* VKI_SIG_DFL or VKI_SIG_IGN 
                              or ptr to our handler */
      UInt skss_flags;
      /* There is no skss_mask, since we know that we will always ask
         for all signals to be blocked in our sighandlers. */
      /* Also there is no skss_restorer. */
   }
   SKSS_Per_Signal;

typedef 
   struct {
      SKSS_Per_Signal skss_per_sig[1+VKI_KNSIG];
   } 
   SKSS;

static SKSS vg_skss;

Bool VG_(is_sig_ign)(Int sigNo)
{
   vg_assert(sigNo >= 1 && sigNo <= VKI_KNSIG);

   return vg_scss.scss_per_sig[sigNo].scss_handler == VKI_SIG_IGN;
}

/* ---------------------------------------------------------------------
   Compute the SKSS required by the current SCSS.
   ------------------------------------------------------------------ */

static 
void pp_SKSS ( void )
{
   Int sig;
   VG_(printf)("\n\nSKSS:\n");
   for (sig = 1; sig <= VKI_KNSIG; sig++) {
      VG_(printf)("sig %d:  handler 0x%x,  flags 0x%x\n", sig,
                  vg_skss.skss_per_sig[sig].skss_handler,
                  vg_skss.skss_per_sig[sig].skss_flags );

   }
}

/* This is the core, clever bit.  Computation is as follows:

   For each signal
      handler = if client has a handler, then our handler
                else if client is DFL, then our handler as well
                else (client must be IGN)
			if (signal == SIGCHLD), then handler is vg_babyeater
			else IGN

   We don't really bother with blocking signals here, because the we
   rely on the proxyLWP having set it as part of its kernel state.
*/
static
void calculate_SKSS_from_SCSS ( SKSS* dst )
{
   Int   sig;
   UInt  scss_flags;
   UInt  skss_flags;

   for (sig = 1; sig <= VKI_KNSIG; sig++) {
      void *skss_handler;
      void *scss_handler;
      
      scss_handler = vg_scss.scss_per_sig[sig].scss_handler;
      scss_flags   = vg_scss.scss_per_sig[sig].scss_flags;

      switch(sig) {
      case VKI_SIGSEGV:
      case VKI_SIGBUS:
      case VKI_SIGFPE:
      case VKI_SIGILL:
	 /* For these, we always want to catch them and report, even
	    if the client code doesn't. */
	 skss_handler = vg_sync_signalhandler;
	 break;

      case VKI_SIGVGINT:
      case VKI_SIGVGKILL:
	 skss_handler = proxy_sigvg_handler;
	 break;

      case VKI_SIGCHLD:
	 if (scss_handler == VKI_SIG_IGN) {
	    skss_handler = vg_babyeater;
	    break;
	 }
	 /* FALLTHROUGH */
      default:
	 if (scss_handler == VKI_SIG_IGN)
	    skss_handler = VKI_SIG_IGN;
	 else 
	    skss_handler = vg_async_signalhandler;
	 break;
      }

      /* Restorer */
      /* 
      Doesn't seem like we can spin this one.
      if (vg_scss.scss_per_sig[sig].scss_restorer != NULL)
         VG_(unimplemented)
            ("sigactions with non-NULL .sa_restorer field");
      */

      /* Flags */

      skss_flags = 0;

      /* SA_NOCLDSTOP: pass to kernel */
      if (scss_flags & VKI_SA_NOCLDSTOP)
         skss_flags |= VKI_SA_NOCLDSTOP;

      /* SA_NOCLDWAIT - don't set */
      /* XXX we could set this if we're not using wait() ourselves for
	 tracking proxyLWPs (ie, have_futex is true in
	 vg_syscalls.c. */

      /* SA_ONESHOT: ignore client setting */
      /*
      if (!(scss_flags & VKI_SA_ONESHOT))
         VG_(unimplemented)
            ("sigactions without SA_ONESHOT");
      vg_assert(scss_flags & VKI_SA_ONESHOT);
      skss_flags |= VKI_SA_ONESHOT;
      */

      /* SA_RESTART: ignore client setting and always set it for us
	 (even though we never rely on the kernel to restart a
	 syscall, we observe whether it wanted to restart the syscall
	 or not, which guides our actions) */
      skss_flags |= VKI_SA_RESTART;

      /* SA_NOMASK: ignore it */

      /* SA_ONSTACK: client setting is irrelevant here */
      /*
      if (scss_flags & VKI_SA_ONSTACK)
         VG_(unimplemented)
            ("signals on an alternative stack (SA_ONSTACK)");
      vg_assert(!(scss_flags & VKI_SA_ONSTACK));
      */
      /* ... but WE ask for on-stack ourselves ... */
      skss_flags |= VKI_SA_ONSTACK;

      /* always ask for SA_SIGINFO */
      skss_flags |= VKI_SA_SIGINFO;

      /* use our own restorer */
      skss_flags |= VKI_SA_RESTORER;

      /* Create SKSS entry for this signal. */

      if (sig != VKI_SIGKILL && sig != VKI_SIGSTOP)
         dst->skss_per_sig[sig].skss_handler = skss_handler;
      else
         dst->skss_per_sig[sig].skss_handler = VKI_SIG_DFL;

      dst->skss_per_sig[sig].skss_flags   = skss_flags;
   }

   /* Sanity checks. */
   vg_assert(dst->skss_per_sig[VKI_SIGKILL].skss_handler 
             == VKI_SIG_DFL);
   vg_assert(dst->skss_per_sig[VKI_SIGSTOP].skss_handler 
             == VKI_SIG_DFL);

   if (0)
      pp_SKSS();
}


/* ---------------------------------------------------------------------
   After a possible SCSS change, update SKSS and the kernel itself.
   ------------------------------------------------------------------ */

void VG_(handle_SCSS_change) ( Bool force_update )
{
   Int            res, sig;
   SKSS           skss_old;
   vki_ksigaction ksa, ksa_old;

   vg_assert(is_correct_sigmask());

   /* Remember old SKSS and calculate new one. */
   skss_old = vg_skss;
   calculate_SKSS_from_SCSS ( &vg_skss );

   /* Compare the new SKSS entries vs the old ones, and update kernel
      where they differ. */
   for (sig = 1; sig <= VKI_KNSIG; sig++) {

      /* Trying to do anything with SIGKILL is pointless; just ignore
         it. */
      if (sig == VKI_SIGKILL || sig == VKI_SIGSTOP)
         continue;

      if (!force_update) {
         if ((skss_old.skss_per_sig[sig].skss_handler
              == vg_skss.skss_per_sig[sig].skss_handler)
             && (skss_old.skss_per_sig[sig].skss_flags
                 == vg_skss.skss_per_sig[sig].skss_flags))
            /* no difference */
            continue;
      }

      ksa.ksa_handler = vg_skss.skss_per_sig[sig].skss_handler;
      ksa.ksa_flags   = vg_skss.skss_per_sig[sig].skss_flags;
      ksa.ksa_restorer = VG_(sigreturn);

      vg_assert(ksa.ksa_flags & VKI_SA_ONSTACK);
      VG_(ksigfillset)( &ksa.ksa_mask );
      VG_(ksigdelset)( &ksa.ksa_mask, VKI_SIGKILL );
      VG_(ksigdelset)( &ksa.ksa_mask, VKI_SIGSTOP );

      if (VG_(clo_trace_signals)) 
         VG_(message)(Vg_DebugMsg, 
            "setting ksig %d to: hdlr 0x%x, flags 0x%x, "
            "mask(63..0) 0x%x 0x%x",
            sig, ksa.ksa_handler,
            ksa.ksa_flags,
            ksa.ksa_mask.ws[1], 
            ksa.ksa_mask.ws[0] 
         );

      res = VG_(ksigaction)( sig, &ksa, &ksa_old );
      vg_assert(res == 0);

      /* Since we got the old sigaction more or less for free, might
         as well extract the maximum sanity-check value from it. */
      if (!force_update) {
         vg_assert(ksa_old.ksa_handler 
                   == skss_old.skss_per_sig[sig].skss_handler);
         vg_assert(ksa_old.ksa_flags 
                   == skss_old.skss_per_sig[sig].skss_flags);
         vg_assert(ksa_old.ksa_restorer 
                   == VG_(sigreturn));
         VG_(ksigaddset)( &ksa_old.ksa_mask, VKI_SIGKILL );
         VG_(ksigaddset)( &ksa_old.ksa_mask, VKI_SIGSTOP );
         vg_assert(VG_(kisfullsigset)( &ksa_old.ksa_mask ));
      }
   }
}


/* ---------------------------------------------------------------------
   Update/query SCSS in accordance with client requests.
   ------------------------------------------------------------------ */

/* Logic for this alt-stack stuff copied directly from do_sigaltstack
   in kernel/signal.[ch] */

/* True if we are on the alternate signal stack.  */
static Int on_sig_stack ( ThreadId tid, Addr m_esp )
{
   ThreadState *tst = VG_(get_ThreadState)(tid);

   return (m_esp - (Addr)tst->altstack.ss_sp 
           < tst->altstack.ss_size);
}

static Int sas_ss_flags ( ThreadId tid, Addr m_esp )
{
   ThreadState *tst = VG_(get_ThreadState)(tid);

   return (tst->altstack.ss_size == 0 
              ? VKI_SS_DISABLE
              : on_sig_stack(tid, m_esp) ? VKI_SS_ONSTACK : 0);
}


void VG_(do__NR_sigaltstack) ( ThreadId tid )
{
   vki_kstack_t* ss;
   vki_kstack_t* oss;
   Addr          m_esp;

   vg_assert(VG_(is_valid_tid)(tid));
   ss    = (vki_kstack_t*)(VG_(threads)[tid].m_ebx);
   oss   = (vki_kstack_t*)(VG_(threads)[tid].m_ecx);
   m_esp = VG_(threads)[tid].m_esp;

   if (VG_(clo_trace_signals))
      VG_(message)(Vg_DebugExtraMsg, 
         "__NR_sigaltstack: tid %d, "
         "ss 0x%x, oss 0x%x (current %%esp %p)",
         tid, (UInt)ss, (UInt)oss, (UInt)m_esp );

   if (oss != NULL) {
      oss->ss_sp    = VG_(threads)[tid].altstack.ss_sp;
      oss->ss_size  = VG_(threads)[tid].altstack.ss_size;
      oss->ss_flags = VG_(threads)[tid].altstack.ss_flags | sas_ss_flags(tid, m_esp);
   }

   if (ss != NULL) {
      if (on_sig_stack(tid, VG_(threads)[tid].m_esp)) {
         SET_SYSCALL_RETVAL(tid, -VKI_EPERM);
         return;
      }
      if (ss->ss_flags != VKI_SS_DISABLE 
          && ss->ss_flags != VKI_SS_ONSTACK 
          && ss->ss_flags != 0) {
         SET_SYSCALL_RETVAL(tid, -VKI_EINVAL);
         return;
      }
      if (ss->ss_flags == VKI_SS_DISABLE) {
         VG_(threads)[tid].altstack.ss_flags = VKI_SS_DISABLE;
      } else {
         if (ss->ss_size < VKI_MINSIGSTKSZ) {
            SET_SYSCALL_RETVAL(tid, -VKI_ENOMEM);
            return;
         }

	 VG_(threads)[tid].altstack.ss_sp   = ss->ss_sp;
	 VG_(threads)[tid].altstack.ss_size = ss->ss_size;
	 VG_(threads)[tid].altstack.ss_flags = 0;
      }
   }
   SET_SYSCALL_RETVAL(tid, 0);
}


void VG_(do__NR_sigaction) ( ThreadId tid )
{
   Int              signo;
   vki_ksigaction*  new_act;
   vki_ksigaction*  old_act;

   vg_assert(is_correct_sigmask());

   vg_assert(VG_(is_valid_tid)(tid));
   signo     = VG_(threads)[tid].m_ebx; /* int sigNo */
   new_act   = (vki_ksigaction*)(VG_(threads)[tid].m_ecx);
   old_act   = (vki_ksigaction*)(VG_(threads)[tid].m_edx);

   if (VG_(clo_trace_signals))
      VG_(message)(Vg_DebugExtraMsg, 
         "__NR_sigaction: tid %d, sigNo %d, "
         "new 0x%x, old 0x%x, new flags 0x%x",
         tid, signo, (UInt)new_act, (UInt)old_act,
         (UInt)(new_act ? new_act->ksa_flags : 0) );

   /* Rule out various error conditions.  The aim is to ensure that if
      when the call is passed to the kernel it will definitely
      succeed. */

   /* Reject out-of-range signal numbers. */
   if (signo < 1 || signo > VKI_KNSIG) goto bad_signo;

   /* don't let them use our signals */
   if ( (signo == VKI_SIGVGINT || signo == VKI_SIGVGKILL)
	&& new_act
	&& !(new_act->ksa_handler == VKI_SIG_DFL || new_act->ksa_handler == VKI_SIG_IGN) )
      goto bad_signo_reserved;

   /* Reject attempts to set a handler (or set ignore) for SIGKILL. */
   if ( (signo == VKI_SIGKILL || signo == VKI_SIGSTOP)
       && new_act
       && new_act->ksa_handler != VKI_SIG_DFL)
      goto bad_sigkill_or_sigstop;

   /* If the client supplied non-NULL old_act, copy the relevant SCSS
      entry into it. */
   if (old_act) {
      old_act->ksa_handler  = vg_scss.scss_per_sig[signo].scss_handler;
      old_act->ksa_flags    = vg_scss.scss_per_sig[signo].scss_flags;
      old_act->ksa_mask     = vg_scss.scss_per_sig[signo].scss_mask;
      old_act->ksa_restorer = vg_scss.scss_per_sig[signo].scss_restorer;
   }

   /* And now copy new SCSS entry from new_act. */
   if (new_act) {
      vg_scss.scss_per_sig[signo].scss_handler  = new_act->ksa_handler;
      vg_scss.scss_per_sig[signo].scss_flags    = new_act->ksa_flags;
      vg_scss.scss_per_sig[signo].scss_mask     = new_act->ksa_mask;
      vg_scss.scss_per_sig[signo].scss_restorer = new_act->ksa_restorer;
   }

   /* All happy bunnies ... */
   if (new_act) {
      VG_(handle_SCSS_change)( False /* lazy update */ );
   }
   SET_SYSCALL_RETVAL(tid, 0);
   return;

  bad_signo:
   if (VG_(needs).core_errors && VG_(clo_verbosity) >= 1)
      VG_(message)(Vg_UserMsg,
                   "Warning: bad signal number %d in sigaction()", 
                   signo);
   SET_SYSCALL_RETVAL(tid, -VKI_EINVAL);
   return;

  bad_signo_reserved:
   if (VG_(needs).core_errors && VG_(clo_verbosity) >= 1) {
      VG_(message)(Vg_UserMsg,
		   "Warning: ignored attempt to set %s handler in sigaction();",
		   signame(signo));
      VG_(message)(Vg_UserMsg,
		   "         the %s signal is used internally by Valgrind", 
		   signame(signo));
   }
   SET_SYSCALL_RETVAL(tid, -VKI_EINVAL);
   return;

  bad_sigkill_or_sigstop:
   if (VG_(needs).core_errors && VG_(clo_verbosity) >= 1)
      VG_(message)(Vg_UserMsg,
		   "Warning: ignored attempt to set %s handler in sigaction();",
		   signame(signo));
      VG_(message)(Vg_UserMsg,
		   "         the %s signal is uncatchable", 
		   signame(signo));
   SET_SYSCALL_RETVAL(tid, -VKI_EINVAL);
   return;
}


static
void do_sigprocmask_bitops ( Int vki_how, 
			     vki_ksigset_t* orig_set,
			     vki_ksigset_t* modifier )
{
   switch (vki_how) {
      case VKI_SIG_BLOCK: 
         VG_(ksigaddset_from_set)( orig_set, modifier );
         break;
      case VKI_SIG_UNBLOCK:
         VG_(ksigdelset_from_set)( orig_set, modifier );
         break;
      case VKI_SIG_SETMASK:
         *orig_set = *modifier;
         break;
      default:
         VG_(core_panic)("do_sigprocmask_bitops");
	 break;
   }
}

/* 
   This updates the thread's signal mask.  There's no such thing as a
   process-wide signal mask.

   Note that the thread signal masks are an implicit part of SCSS,
   which is why this routine is allowed to mess with them.  
*/
static
void do_setmask ( ThreadId tid,
                  Int how,
                  vki_ksigset_t* newset,
		  vki_ksigset_t* oldset )
{
   vg_assert(is_correct_sigmask());

   if (VG_(clo_trace_signals))
      VG_(message)(Vg_DebugExtraMsg, 
		   "do_setmask: tid = %d how = %d (%s), set = %p %08x%08x", 
		   tid, how,
		   how==VKI_SIG_BLOCK ? "SIG_BLOCK" : (
		      how==VKI_SIG_UNBLOCK ? "SIG_UNBLOCK" : (
			 how==VKI_SIG_SETMASK ? "SIG_SETMASK" : "???")),
		   newset, newset ? newset->ws[1] : 0, newset ? newset->ws[0] : 0
	 );

   /* Just do this thread. */
   vg_assert(VG_(is_valid_tid)(tid));
   if (oldset) {
      *oldset = VG_(threads)[tid].eff_sig_mask;
      if (VG_(clo_trace_signals))
	      VG_(message)(Vg_DebugExtraMsg, 
			   "\toldset=%p %08x%08x",
			   oldset, oldset->ws[1], oldset->ws[0]);
   }
   if (newset) {
      do_sigprocmask_bitops (how, &VG_(threads)[tid].sig_mask, newset );
      VG_(ksigdelset)(&VG_(threads)[tid].sig_mask, VKI_SIGKILL);
      VG_(ksigdelset)(&VG_(threads)[tid].sig_mask, VKI_SIGSTOP);
      VG_(proxy_setsigmask)(tid);
   }

   vg_assert(is_correct_sigmask());
}


void VG_(do__NR_sigprocmask) ( ThreadId tid,
                               Int how, 
                               vki_ksigset_t* set,
                               vki_ksigset_t* oldset )
{
   switch(how) {
   case VKI_SIG_BLOCK:
   case VKI_SIG_UNBLOCK:
   case VKI_SIG_SETMASK:
      vg_assert(VG_(is_valid_tid)(tid));
      /* Syscall returns 0 (success) to its thread. Set this up before
	 calling do_setmask() because we may get a signal as part of
	 setting the mask, which will confuse things.
       */
      SET_SYSCALL_RETVAL(tid, 0);
      do_setmask ( tid, how, set, oldset );

      VG_(route_signals)();	/* if we're routing, do something before returning */
      break;

   default:
      VG_(message)(Vg_DebugMsg, 
                  "sigprocmask: unknown `how' field %d", how);
      SET_SYSCALL_RETVAL(tid, -VKI_EINVAL);
      break;
   }
}


void VG_(do_pthread_sigmask_SCSS_upd) ( ThreadId tid,
                                        Int how, 
                                        vki_ksigset_t* set,
                                        vki_ksigset_t* oldset )
{
   /* Assume that how has been validated by caller. */
   vg_assert(how == VKI_SIG_BLOCK || how == VKI_SIG_UNBLOCK 
                                  || how == VKI_SIG_SETMASK);
   vg_assert(VG_(is_valid_tid)(tid));
   do_setmask ( tid, how, set, oldset );
   /* The request return code is set in do_pthread_sigmask */
}


void VG_(send_signal_to_thread) ( ThreadId thread, Int sig )
{
   ThreadState *tst;

   vg_assert(VG_(is_valid_tid)(thread));
   vg_assert(sig >= 1 && sig <= VKI_KNSIG);
   
   tst = VG_(get_ThreadState)(thread);
   vg_assert(tst->proxy != NULL);

   VG_(proxy_sendsig)(thread, sig);
}


/* ---------------------------------------------------------------------
   LOW LEVEL STUFF TO DO WITH SIGNALS: IMPLEMENTATION
   ------------------------------------------------------------------ */

/* ---------------------------------------------------------------------
   Handy utilities to block/restore all host signals.
   ------------------------------------------------------------------ */

/* Block all host signals, dumping the old mask in *saved_mask. */
void VG_(block_all_host_signals) ( /* OUT */ vki_ksigset_t* saved_mask )
{
   Int           ret;
   vki_ksigset_t block_procmask;
   VG_(ksigfillset)(&block_procmask);
   ret = VG_(ksigprocmask)
            (VKI_SIG_SETMASK, &block_procmask, saved_mask);
   vg_assert(ret == 0);
}

/* Restore the blocking mask using the supplied saved one. */
void VG_(restore_all_host_signals) ( /* IN */ vki_ksigset_t* saved_mask )
{
   Int ret;
   ret = VG_(ksigprocmask)(VKI_SIG_SETMASK, saved_mask, NULL);
   vg_assert(ret == 0);
}

/* Sanity check - check the scheduler LWP has all the signals blocked
   it is supposed to have blocked. */
static Bool is_correct_sigmask(void)
{
   vki_ksigset_t mask;
   Bool ret = True;

   vg_assert(VG_(gettid)() == VG_(main_pid));

#ifdef DEBUG_SIGNALS
   VG_(ksigprocmask)(VKI_SIG_SETMASK, NULL, &mask);

   /* unresumable signals */
   
   ret = ret && !VG_(ksigismember)(&mask, VKI_SIGSEGV);
   VG_(ksigaddset)(&mask, VKI_SIGSEGV);

   ret = ret && !VG_(ksigismember)(&mask, VKI_SIGBUS);
   VG_(ksigaddset)(&mask, VKI_SIGBUS);

   ret = ret && !VG_(ksigismember)(&mask, VKI_SIGFPE);
   VG_(ksigaddset)(&mask, VKI_SIGFPE);

   ret = ret && !VG_(ksigismember)(&mask, VKI_SIGILL);
   VG_(ksigaddset)(&mask, VKI_SIGILL);

   /* unblockable signals (doesn't really matter if these are
      already present) */
   VG_(ksigaddset)(&mask, VKI_SIGSTOP);
   VG_(ksigaddset)(&mask, VKI_SIGKILL);

   ret = ret && VG_(kisfullsigset)(&mask);
#endif /* DEBUG_SIGNALS */

   return ret;
}

/* Set the signal mask for the scheduer LWP; this should be set once
   and left that way - all async signal handling is done in the proxy
   LWPs. */
static void set_main_sigmask(void)
{
   vki_ksigset_t mask;

   VG_(ksigfillset)(&mask);
   VG_(ksigdelset)(&mask, VKI_SIGSEGV);
   VG_(ksigdelset)(&mask, VKI_SIGBUS);
   VG_(ksigdelset)(&mask, VKI_SIGFPE);
   VG_(ksigdelset)(&mask, VKI_SIGILL);

   VG_(ksigprocmask)(VKI_SIG_SETMASK, &mask, NULL);

   vg_assert(is_correct_sigmask());
}

/* ---------------------------------------------------------------------
   The signal simulation proper.  A simplified version of what the 
   Linux kernel does.
   ------------------------------------------------------------------ */

/* A structure in which to save the application's registers
   during the execution of signal handlers. */

typedef
   struct {
      /* There are two different stack frame formats, depending on
	 whether the client set the SA_SIGINFO flag for the handler.
	 This structure is put onto the client's stack as part of
	 signal delivery, and therefore appears as the signal
	 handler's arguments.

	 The first two words are common for both frame formats -
	 they're the return address and the signal number. */

      /* Sig handler's (bogus) return address */
      Addr retaddr;
      /* The arg to the sig handler.  We need to inspect this after
         the handler returns, but it's unreasonable to assume that the
         handler won't change it.  So we keep a second copy of it in
         sigNo_private. */
      Int  sigNo;

      /* This is where the two frames start differing. */
      union {
	 struct {		/* set SA_SIGINFO */
	    /* ptr to siginfo_t. */
	    Addr psigInfo;

	    /* ptr to ucontext */
	    Addr puContext;
	 } sigInfo;
	 struct vki_sigcontext sigContext; /* did not set SA_SIGINFO */
      } handlerArgs;

      /* The rest are private fields which the handler is unaware of. */

      /* Sanity check word. */
      UInt magicPI;
      /* pointed to by psigInfo */
      vki_ksiginfo_t sigInfo;
      /* pointed to by puContext */
      struct vki_ucontext uContext;

      /* Safely-saved version of sigNo, as described above. */
      Int  sigNo_private;
      /* Saved processor state. */
      UInt m_sse[VG_SIZE_OF_SSESTATE_W];

      UInt m_eax;
      UInt m_ecx;
      UInt m_edx;
      UInt m_ebx;
      UInt m_ebp;
      UInt m_esp;
      UInt m_esi;
      UInt m_edi;
      UInt m_eflags;
      Addr m_eip;

      UInt sh_eax;
      UInt sh_ebx;
      UInt sh_ecx;
      UInt sh_edx;
      UInt sh_esi;
      UInt sh_edi;
      UInt sh_ebp;
      UInt sh_esp;
      UInt sh_eflags;

      /* saved signal mask to be restored when handler returns */
      vki_ksigset_t	mask;

      /* Scheduler-private stuff: what was the thread's status prior to
         delivering this signal? */
      ThreadStatus status;
      /* Sanity check word.  Is the highest-addressed word; do not
         move!*/
      UInt magicE;
   }
   VgSigFrame;


/* Make up a plausible-looking thread state from the thread's current state */
static void synth_ucontext(ThreadId tid, const vki_ksiginfo_t *si, 
			   const vki_ksigset_t *set, struct vki_ucontext *uc)
{
   ThreadState *tst = VG_(get_ThreadState)(tid);
   struct vki_sigcontext *sc = &uc->uc_mcontext;

   VG_(memset)(uc, 0, sizeof(*uc));

   uc->uc_flags = 0;
   uc->uc_link = 0;
   uc->uc_sigmask = *set;
   uc->uc_stack = tst->altstack;

#define SC(reg)	sc->reg = tst->m_##reg
   SC(gs);
   SC(fs);
   SC(es);
   SC(ds);

   SC(edi);
   SC(esi);
   SC(ebp);
   SC(esp);
   SC(ebx);
   SC(edx);
   SC(ecx);
   SC(eax);

   SC(eip);
   SC(cs);
   SC(eflags);
   SC(ss);
   /* XXX esp_at_signal */
   /* XXX trapno */
   /* XXX err */
#undef SC

   sc->cr2 = (UInt)si->_sifields._sigfault._addr;
}

/* Set up a stack frame (VgSigContext) for the client's signal
   handler.  This includes the signal number and a bogus return
   address.  */
static
void vg_push_signal_frame ( ThreadId tid, const vki_ksiginfo_t *siginfo )
{
   Int          i;
   Addr         esp, esp_top_of_frame;
   VgSigFrame*  frame;
   ThreadState* tst;
   Int		sigNo = siginfo->si_signo;

   vg_assert(sigNo >= 1 && sigNo <= VKI_KNSIG);
   vg_assert(VG_(is_valid_tid)(tid));
   tst = & VG_(threads)[tid];

   if (VG_(clo_trace_signals))
      VG_(message)(Vg_DebugMsg, 
         "vg_push_signal_frame (thread %d): signal %d", tid, sigNo);

   if (/* this signal asked to run on an alt stack */
       (vg_scss.scss_per_sig[sigNo].scss_flags & VKI_SA_ONSTACK )
       && /* there is a defined and enabled alt stack, which we're not
             already using.  Logic from get_sigframe in
             arch/i386/kernel/signal.c. */
          sas_ss_flags(tid, tst->m_esp) == 0
      ) {
      esp_top_of_frame 
         = (Addr)(tst->altstack.ss_sp) + tst->altstack.ss_size;
      if (VG_(clo_trace_signals))
         VG_(message)(Vg_DebugMsg,
            "delivering signal %d (%s) to thread %d: on ALT STACK", 
            sigNo, signame(sigNo), tid );

      /* Signal delivery to tools */
      VG_TRACK( pre_deliver_signal, tid, sigNo, /*alt_stack*/True );
      
   } else {
      esp_top_of_frame = tst->m_esp;

      /* Signal delivery to tools */
      VG_TRACK( pre_deliver_signal, tid, sigNo, /*alt_stack*/False );
   }

   esp = esp_top_of_frame;
   esp -= sizeof(VgSigFrame);
   frame = (VgSigFrame*)esp;

   /* For tracking memory events, indicate the entire frame has been
    * allocated, but pretend that only the first four words are written */
   VG_TRACK( new_mem_stack_signal, (Addr)frame, sizeof(VgSigFrame) );

   /* Assert that the frame is placed correctly. */
   vg_assert( (sizeof(VgSigFrame) & 0x3) == 0 );
   vg_assert( ((Char*)(&frame->magicE)) + sizeof(UInt) 
              == ((Char*)(esp_top_of_frame)) );

   /* retaddr, sigNo, psigInfo, puContext fields are to be written */
   VG_TRACK( pre_mem_write, Vg_CoreSignal, tid, "signal handler frame", 
                            (Addr)frame, offsetof(VgSigFrame, handlerArgs) );
   frame->retaddr    = (UInt)VG_(client_trampoline_code)+VG_(tramp_sigreturn_offset);
   frame->sigNo      = sigNo;
   frame->sigNo_private = sigNo;
   VG_TRACK( post_mem_write, (Addr)frame, offsetof(VgSigFrame, handlerArgs) );

   if (vg_scss.scss_per_sig[sigNo].scss_flags & VKI_SA_SIGINFO) {
      /* if the client asked for a siginfo delivery, then build the stack that way */
      VG_TRACK( pre_mem_write, Vg_CoreSignal, tid, "signal handler frame (siginfo)", 
		(Addr)&frame->handlerArgs, sizeof(frame->handlerArgs.sigInfo) );
      frame->handlerArgs.sigInfo.psigInfo   = (Addr)&frame->sigInfo;
      frame->handlerArgs.sigInfo.puContext = (Addr)&frame->uContext;
      VG_TRACK( post_mem_write, (Addr)&frame->handlerArgs, sizeof(frame->handlerArgs.sigInfo) );

      VG_TRACK( pre_mem_write, Vg_CoreSignal, tid, "signal handler frame (siginfo)", 
		(Addr)&frame->sigInfo, sizeof(frame->sigInfo) );
      VG_(memcpy)(&frame->sigInfo, siginfo, sizeof(vki_ksiginfo_t));
      VG_TRACK( post_mem_write, (Addr)&frame->sigInfo, sizeof(frame->sigInfo) );

      VG_TRACK( pre_mem_write, Vg_CoreSignal, tid, "signal handler frame (siginfo)", 
		(Addr)&frame->uContext, sizeof(frame->uContext) );
      synth_ucontext(tid, siginfo, &vg_scss.scss_per_sig[sigNo].scss_mask, &frame->uContext);
      VG_TRACK( post_mem_write, (Addr)&frame->uContext, sizeof(frame->uContext) );
   } else {
      struct vki_ucontext uc;

      /* otherwise just put the sigcontext there */

      synth_ucontext(tid, siginfo, &vg_scss.scss_per_sig[sigNo].scss_mask, &uc);

      VG_TRACK( pre_mem_write, Vg_CoreSignal, tid, "signal handler frame (sigcontext)", 
		(Addr)&frame->handlerArgs, sizeof(frame->handlerArgs.sigContext) );
      VG_(memcpy)(&frame->handlerArgs.sigContext, &uc.uc_mcontext, 
		  sizeof(struct vki_sigcontext));
      VG_TRACK( post_mem_write, (Addr)&frame->handlerArgs, 
		sizeof(frame->handlerArgs.sigContext) );
      
      frame->handlerArgs.sigContext.oldmask = tst->sig_mask.ws[0];
   }

   frame->magicPI    = 0x31415927;

   for (i = 0; i < VG_SIZE_OF_SSESTATE_W; i++)
      frame->m_sse[i] = tst->m_sse[i];

   frame->m_eax      = tst->m_eax;
   frame->m_ecx      = tst->m_ecx;
   frame->m_edx      = tst->m_edx;
   frame->m_ebx      = tst->m_ebx;
   frame->m_ebp      = tst->m_ebp;
   frame->m_esp      = tst->m_esp;
   frame->m_esi      = tst->m_esi;
   frame->m_edi      = tst->m_edi;
   frame->m_eflags   = tst->m_eflags;
   frame->m_eip      = tst->m_eip;

   if (VG_(needs).shadow_regs) {
      frame->sh_eax     = tst->sh_eax;
      frame->sh_ecx     = tst->sh_ecx;
      frame->sh_edx     = tst->sh_edx;
      frame->sh_ebx     = tst->sh_ebx;
      frame->sh_ebp     = tst->sh_ebp;
      frame->sh_esp     = tst->sh_esp;
      frame->sh_esi     = tst->sh_esi;
      frame->sh_edi     = tst->sh_edi;
      frame->sh_eflags  = tst->sh_eflags;
   }

   frame->mask = tst->sig_mask;

   /* If the thread is currently blocked in a syscall, we want it to
      resume as runnable. */
   if (tst->status == VgTs_WaitSys)
      frame->status = VgTs_Runnable;
   else
      frame->status = tst->status;

   frame->magicE     = 0x27182818;

   /* Ensure 'tid' and 'tst' correspond */
   vg_assert(& VG_(threads)[tid] == tst);
   /* Set the thread so it will next run the handler. */
   /* tst->m_esp  = esp; */
   SET_SIGNAL_ESP(tid, esp);

   tst->m_eip  = (Addr)vg_scss.scss_per_sig[sigNo].scss_handler;
   /* This thread needs to be marked runnable, but we leave that the
      caller to do. */

   if (0)
      VG_(printf)("pushed signal frame; %%ESP now = %p, next %%EBP = %p, status=%d\n", 
		  esp, tst->m_eip, tst->status);
}

/* Clear the signal frame created by vg_push_signal_frame, restore the
   simulated machine state, and return the signal number that the
   frame was for. */
static
Int vg_pop_signal_frame ( ThreadId tid )
{
   Addr          esp;
   Int           sigNo, i;
   VgSigFrame*   frame;
   ThreadState*  tst;

   vg_assert(VG_(is_valid_tid)(tid));
   tst = & VG_(threads)[tid];

   /* Correctly reestablish the frame base address. */
   esp   = tst->m_esp;
   frame = (VgSigFrame*)
              (esp -4 /* because the handler's RET pops the RA */
                  +20 /* because signalreturn_bogusRA pushes 5 words */);

   vg_assert(frame->magicPI == 0x31415927);
   vg_assert(frame->magicE  == 0x27182818);
   if (VG_(clo_trace_signals))
      VG_(message)(Vg_DebugMsg, 
         "vg_pop_signal_frame (thread %d): valid magic; EIP=%p", tid, frame->m_eip);

   /* Mark the frame structure as nonaccessible. */
   VG_TRACK( die_mem_stack_signal, (Addr)frame, sizeof(VgSigFrame) );

   /* restore machine state */
   for (i = 0; i < VG_SIZE_OF_SSESTATE_W; i++)
      tst->m_sse[i] = frame->m_sse[i];

   tst->m_eax     = frame->m_eax;
   tst->m_ecx     = frame->m_ecx;
   tst->m_edx     = frame->m_edx;
   tst->m_ebx     = frame->m_ebx;
   tst->m_ebp     = frame->m_ebp; 
   tst->m_esp     = frame->m_esp;
   tst->m_esi     = frame->m_esi;
   tst->m_edi     = frame->m_edi;
   tst->m_eflags  = frame->m_eflags;
   tst->m_eip     = frame->m_eip;

   if (VG_(needs).shadow_regs) {
      tst->sh_eax     = frame->sh_eax;
      tst->sh_ecx     = frame->sh_ecx;
      tst->sh_edx     = frame->sh_edx;
      tst->sh_ebx     = frame->sh_ebx;
      tst->sh_ebp     = frame->sh_ebp; 
      tst->sh_esp     = frame->sh_esp;
      tst->sh_esi     = frame->sh_esi;
      tst->sh_edi     = frame->sh_edi;
      tst->sh_eflags  = frame->sh_eflags;
   }
   
   /* don't use the copy exposed to the handler; it might have changed
      it. */
   sigNo          = frame->sigNo_private; 

   /* And restore the thread's status to what it was before the signal
      was delivered. */
   tst->status    = frame->status;

   tst->sig_mask  = frame->mask;
   VG_(proxy_setsigmask)(tid);

   /* Notify tools */
   VG_TRACK( post_deliver_signal, tid, sigNo );

   return sigNo;
}


/* A handler is returning.  Restore the machine state from the stacked
   VgSigContext and continue with whatever was going on before the
   handler ran.  Returns the SA_RESTART syscall-restartability-status
   of the delivered signal. */

Bool VG_(signal_returns) ( ThreadId tid )
{
   Int            sigNo;

   /* Pop the signal frame and restore tid's status to what it was
      before the signal was delivered. */
   sigNo = vg_pop_signal_frame(tid);

   vg_assert(sigNo >= 1 && sigNo <= VKI_KNSIG);

   /* Scheduler now can resume this thread, or perhaps some other.
      Tell the scheduler whether or not any syscall interrupted by
      this signal should be restarted, if possible, or no.  This is
      only used for nanosleep; all other blocking syscalls are handled
      in VG_(deliver_signal)().
   */
   return 
      (vg_scss.scss_per_sig[sigNo].scss_flags & VKI_SA_RESTART)
         ? True 
         : False;
}

static const Char *signame(Int sigNo)
{
   static Char buf[10];

   switch(sigNo) {
#define S(x)	case VKI_##x: return #x
      S(SIGHUP);
      S(SIGINT);
      S(SIGQUIT);
      S(SIGILL);
      S(SIGTRAP);
      S(SIGABRT);
      S(SIGBUS);
      S(SIGFPE);
      S(SIGKILL);
      S(SIGUSR1);
      S(SIGUSR2);
      S(SIGSEGV);
      S(SIGPIPE);
      S(SIGALRM);
      S(SIGTERM);
      S(SIGSTKFLT);
      S(SIGCHLD);
      S(SIGCONT);
      S(SIGSTOP);
      S(SIGTSTP);
      S(SIGTTIN);
      S(SIGTTOU);
      S(SIGURG);
      S(SIGXCPU);
      S(SIGXFSZ);
      S(SIGVTALRM);
      S(SIGPROF);
      S(SIGWINCH);
      S(SIGIO);
      S(SIGPWR);
      S(SIGUNUSED);
#undef S

   case VKI_SIGRTMIN ... VKI_SIGRTMAX:
      VG_(sprintf)(buf, "SIGRT%d", sigNo);
      return buf;

   default:
      VG_(sprintf)(buf, "SIG%d", sigNo);
      return buf;
   }
}

/* Hit ourselves with a signal using the default handler */
void VG_(kill_self)(Int sigNo)
{
   vki_ksigset_t	mask, origmask;
   vki_ksigaction	sa, origsa;   

   sa.ksa_handler = VKI_SIG_DFL;
   sa.ksa_flags = 0;
   sa.ksa_restorer = 0;
   VG_(ksigemptyset)(&sa.ksa_mask);
      
   VG_(ksigaction)(sigNo, &sa, &origsa);

   VG_(ksigfillset)(&mask);
   VG_(ksigdelset)(&mask, sigNo);
   VG_(ksigprocmask)(VKI_SIG_SETMASK, &mask, &origmask);

   VG_(ktkill)(VG_(getpid)(), sigNo);

   VG_(ksigaction)(sigNo, &origsa, NULL);
   VG_(ksigprocmask)(VKI_SIG_SETMASK, &origmask, NULL);
}

/*
  Dump core
   
  Generate a standard ELF core file corresponding to the client state
  at the time of a crash.
 */
#include <elf.h>
#ifndef NT_PRXFPREG
#define NT_PRXFPREG     0x46e62b7f      /* copied from gdb5.1/include/elf/common.h */
#endif /* NT_PRXFPREG */

/* If true, then this Segment may be mentioned in the core */
static Bool may_dump(const Segment *seg)
{
   return (seg->flags & SF_VALGRIND) == 0 && VG_(is_client_addr)(seg->addr);
}

/* If true, then this Segment's contents will be in the core */
static Bool should_dump(const Segment *seg)
{
   return may_dump(seg); // && (seg->prot & VKI_PROT_WRITE);
}

static void fill_ehdr(Elf32_Ehdr *ehdr, Int num_phdrs)
{
   VG_(memset)(ehdr, 0, sizeof(ehdr));

   VG_(memcpy)(ehdr->e_ident, ELFMAG, SELFMAG);
   ehdr->e_ident[EI_CLASS] = ELFCLASS32;
   ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
   ehdr->e_ident[EI_VERSION] = EV_CURRENT;

   ehdr->e_type = ET_CORE;
   ehdr->e_machine = EM_386;
   ehdr->e_version = EV_CURRENT;
   ehdr->e_entry = 0;
   ehdr->e_phoff = sizeof(Elf32_Ehdr);
   ehdr->e_shoff = 0;
   ehdr->e_flags = 0;
   ehdr->e_ehsize = sizeof(Elf32_Ehdr);
   ehdr->e_phentsize = sizeof(Elf32_Phdr);
   ehdr->e_phnum = num_phdrs;
   ehdr->e_shentsize = 0;
   ehdr->e_shnum = 0;
   ehdr->e_shstrndx = 0;

}

static void fill_phdr(Elf32_Phdr *phdr, const Segment *seg, UInt off, Bool write)
{
   write = write && should_dump(seg);

   VG_(memset)(phdr, 0, sizeof(*phdr));

   phdr->p_type = PT_LOAD;
   phdr->p_offset = off;
   phdr->p_vaddr = seg->addr;
   phdr->p_paddr = 0;
   phdr->p_filesz = write ? seg->len : 0;
   phdr->p_memsz = seg->len;
   phdr->p_flags = 0;

   if (seg->prot & VKI_PROT_READ)
      phdr->p_flags |= PF_R;
   if (seg->prot & VKI_PROT_WRITE)
      phdr->p_flags |= PF_W;
   if (seg->prot & VKI_PROT_EXEC)
      phdr->p_flags |= PF_X;

   phdr->p_align = VKI_BYTES_PER_PAGE;
}

struct note {
   struct note *next;
   Elf32_Nhdr note;
   Char name[0];
};

static UInt note_size(const struct note *n)
{
   return sizeof(Elf32_Nhdr) + ROUNDUP(VG_(strlen)(n->name)+1, 4) + ROUNDUP(n->note.n_descsz, 4);
}

static void add_note(struct note **list, const Char *name, UInt type, const void *data, UInt datasz)
{
   Int namelen = VG_(strlen)(name)+1;
   Int notelen = sizeof(struct note) + 
      ROUNDUP(namelen, 4) + 
      ROUNDUP(datasz, 4);
   struct note *n = VG_(arena_malloc)(VG_AR_CORE, notelen);

   VG_(memset)(n, 0, notelen);

   n->next = *list;
   *list = n;

   n->note.n_type = type;
   n->note.n_namesz = namelen;
   n->note.n_descsz = datasz;

   VG_(memcpy)(n->name, name, namelen);
   VG_(memcpy)(n->name+ROUNDUP(namelen,4), data, datasz);
}

static void write_note(Int fd, const struct note *n)
{
   VG_(write)(fd, &n->note, note_size(n));
}

static void fill_prpsinfo(const ThreadState *tst, struct elf_prpsinfo *prpsinfo)
{
   Char *name;

   VG_(memset)(prpsinfo, 0, sizeof(*prpsinfo));

   switch(tst->status) {
   case VgTs_Runnable:
      prpsinfo->pr_sname = 'R';
      break;

   case VgTs_WaitJoinee:
      prpsinfo->pr_sname = 'Z';
      prpsinfo->pr_zomb = 1;
      break;

   case VgTs_WaitJoiner:
   case VgTs_WaitMX:
   case VgTs_WaitCV:
   case VgTs_WaitSys:
   case VgTs_Sleeping:
      prpsinfo->pr_sname = 'S';
      break;

   case VgTs_Empty:
      /* ? */
      break;
   }

   prpsinfo->pr_uid = 0;
   prpsinfo->pr_gid = 0;
   
   name = VG_(resolve_filename)(VG_(clexecfd));

   if (name != NULL) {
      Char *n = name+VG_(strlen)(name)-1;

      while(n > name && *n != '/')
	 n--;
      if (n != name)
	 n++;

      VG_(strncpy)(prpsinfo->pr_fname, n, sizeof(prpsinfo->pr_fname));
   }
}

static void fill_prstatus(const ThreadState *tst, struct elf_prstatus *prs, const vki_ksiginfo_t *si)
{
   struct user_regs_struct *regs;

   VG_(memset)(prs, 0, sizeof(*prs));

   prs->pr_info.si_signo = si->si_signo;
   prs->pr_info.si_code = si->si_code;
   prs->pr_info.si_errno = 0;

   prs->pr_cursig = si->si_signo;

   prs->pr_pid = VG_(main_pid) + tst->tid;	/* just to distinguish threads from each other */
   prs->pr_ppid = 0;
   prs->pr_pgrp = VG_(main_pgrp);
   prs->pr_sid = VG_(main_pgrp);
   
   regs = (struct user_regs_struct *)prs->pr_reg;

   vg_assert(sizeof(*regs) == sizeof(prs->pr_reg));

   if (VG_(is_running_thread)(tst->tid)) {
      regs->eflags = VG_(baseBlock)[VGOFF_(m_eflags)];
      regs->esp = VG_(baseBlock)[VGOFF_(m_esp)];
      regs->eip = VG_(baseBlock)[VGOFF_(m_eip)];

      regs->ebx = VG_(baseBlock)[VGOFF_(m_ebx)];
      regs->ecx = VG_(baseBlock)[VGOFF_(m_ecx)];
      regs->edx = VG_(baseBlock)[VGOFF_(m_edx)];
      regs->esi = VG_(baseBlock)[VGOFF_(m_esi)];
      regs->edi = VG_(baseBlock)[VGOFF_(m_edi)];
      regs->ebp = VG_(baseBlock)[VGOFF_(m_ebp)];
      regs->eax = VG_(baseBlock)[VGOFF_(m_eax)];

      regs->cs = VG_(baseBlock)[VGOFF_(m_cs)];
      regs->ds = VG_(baseBlock)[VGOFF_(m_ds)];
      regs->ss = VG_(baseBlock)[VGOFF_(m_ss)];
      regs->es = VG_(baseBlock)[VGOFF_(m_es)];
      regs->fs = VG_(baseBlock)[VGOFF_(m_fs)];
      regs->gs = VG_(baseBlock)[VGOFF_(m_gs)];
   } else {
      regs->eflags = tst->m_eflags;
      regs->esp = tst->m_esp;
      regs->eip = tst->m_eip;

      regs->ebx = tst->m_ebx;
      regs->ecx = tst->m_ecx;
      regs->edx = tst->m_edx;
      regs->esi = tst->m_esi;
      regs->edi = tst->m_edi;
      regs->ebp = tst->m_ebp;
      regs->eax = tst->m_eax;

      regs->cs = tst->m_cs;
      regs->ds = tst->m_ds;
      regs->ss = tst->m_ss;
      regs->es = tst->m_es;
      regs->fs = tst->m_fs;
      regs->gs = tst->m_gs;
   }
}

static void fill_fpu(const ThreadState *tst, elf_fpregset_t *fpu)
{
   const Char *from;

   if (VG_(is_running_thread)(tst->tid)) {
      from = (const Char *)&VG_(baseBlock)[VGOFF_(m_ssestate)];
   } else {
      from = (const Char *)&tst->m_sse;
   }

   if (VG_(have_ssestate)) {
      UShort *to;
      Int i;

      /* This is what the kernel does */
      VG_(memcpy)(fpu, from, 7*sizeof(long));
   
      to = (UShort *)&fpu->st_space[0];
      from += 18 * sizeof(UShort);

      for(i = 0; i < 8; i++, to += 5, from += 8) 
	 VG_(memcpy)(to, from, 5*sizeof(UShort));
   } else
      VG_(memcpy)(fpu, from, sizeof(*fpu));
}

static void fill_xfpu(const ThreadState *tst, elf_fpxregset_t *xfpu)
{
   UShort *from;

   if (VG_(is_running_thread)(tst->tid)) 
      from = (UShort *)&VG_(baseBlock)[VGOFF_(m_ssestate)];
   else 
      from = (UShort *)tst->m_sse;

   VG_(memcpy)(xfpu, from, sizeof(*xfpu));
}

static void make_coredump(ThreadId tid, const vki_ksiginfo_t *si, UInt max_size)
{
   Char buf[1000];
   Char *basename = "vgcore";
   Char *coreext = "";
   Int seq = 0;
   Int core_fd;
   Segment *seg;
   Elf32_Ehdr ehdr;
   Elf32_Phdr *phdrs;
   Int num_phdrs;
   Int i;
   UInt off;
   struct note *notelist, *note;
   UInt notesz;
   struct elf_prpsinfo prpsinfo;
   struct elf_prstatus prstatus;

   if (VG_(clo_log_name) != NULL) {
      coreext = ".core";
      basename = VG_(clo_log_name);
   }

   for(;;) {
      if (seq == 0)
	 VG_(sprintf)(buf, "%s%s.pid%d",
		      basename, coreext, VG_(main_pid));
      else
	 VG_(sprintf)(buf, "%s%s.pid%d.%d",
		      basename, coreext, VG_(main_pid), seq);
      seq++;

      core_fd = VG_(open)(buf, 			   
			  VKI_O_CREAT|VKI_O_WRONLY|VKI_O_EXCL|VKI_O_TRUNC, 
			  VKI_S_IRUSR|VKI_S_IWUSR);
      if (core_fd >= 0)
	 break;

      if (core_fd != -VKI_EEXIST)
	 return;		/* can't create file */
   }

   /* First, count how many memory segments to dump */
   num_phdrs = 1;		/* start with notes */
   for(seg = VG_(first_segment)();
       seg != NULL;
       seg = VG_(next_segment)(seg)) {
      if (!may_dump(seg))
	 continue;

      num_phdrs++;
   }

   fill_ehdr(&ehdr, num_phdrs);

   /* Second, work out their layout */
   phdrs = VG_(arena_malloc)(VG_AR_CORE, sizeof(*phdrs) * num_phdrs);

   for(i = 1; i < VG_N_THREADS; i++) {
      elf_fpregset_t fpu;

      if (VG_(threads)[i].status == VgTs_Empty)
	 continue;

      if (VG_(have_ssestate)) {
	 elf_fpxregset_t xfpu;

	 fill_xfpu(&VG_(threads)[i], &xfpu);
	 add_note(&notelist, "LINUX", NT_PRXFPREG, &xfpu, sizeof(xfpu));
      }

      fill_fpu(&VG_(threads)[i], &fpu);
      add_note(&notelist, "CORE", NT_FPREGSET, &fpu, sizeof(fpu));

      fill_prstatus(&VG_(threads)[i], &prstatus, si);
      add_note(&notelist, "CORE", NT_PRSTATUS, &prstatus, sizeof(prstatus));
   }

   fill_prpsinfo(&VG_(threads)[tid], &prpsinfo);
   add_note(&notelist, "CORE", NT_PRPSINFO, &prpsinfo, sizeof(prpsinfo));

   for(note = notelist, notesz = 0; note != NULL; note = note->next)
      notesz += note_size(note);

   off = sizeof(ehdr) + sizeof(*phdrs) * num_phdrs;

   phdrs[0].p_type = PT_NOTE;
   phdrs[0].p_offset = off;
   phdrs[0].p_vaddr = 0;
   phdrs[0].p_paddr = 0;
   phdrs[0].p_filesz = notesz;
   phdrs[0].p_memsz = 0;
   phdrs[0].p_flags = 0;
   phdrs[0].p_align = 0;

   off += notesz;

   off = PGROUNDUP(off);

   for(seg = VG_(first_segment)(), i = 1;
       seg != NULL;
       seg = VG_(next_segment)(seg), i++) {
      if (!may_dump(seg))
	 continue;

      fill_phdr(&phdrs[i], seg, off, (seg->len + off) < max_size);
      
      off += phdrs[i].p_filesz;
   }

   /* write everything out */
   VG_(write)(core_fd, &ehdr, sizeof(ehdr));
   VG_(write)(core_fd, phdrs, sizeof(*phdrs) * num_phdrs);

   for(note = notelist; note != NULL; note = note->next)
      write_note(core_fd, note);
   
   VG_(lseek)(core_fd, phdrs[1].p_offset, VKI_SEEK_SET);

   for(seg = VG_(first_segment)(), i = 1;
       seg != NULL;
       seg = VG_(next_segment)(seg), i++) {
      if (!should_dump(seg))
	 continue;

      vg_assert(VG_(lseek)(core_fd, 0, VKI_SEEK_CUR) == phdrs[i].p_offset);
      if (phdrs[i].p_filesz > 0)
	 VG_(write)(core_fd, (void *)seg->addr, seg->len);
   }

   VG_(close)(core_fd);
}

/* 
   Perform the default action of a signal.  Returns if the default
   action isn't fatal.

   If we're not being quiet, then print out some more detail about
   fatal signals (esp. core dumping signals).
 */
static void vg_default_action(const vki_ksiginfo_t *info, ThreadId tid)
{
   Int  sigNo     = info->si_signo;
   Bool terminate = False;
   Bool core      = False;

   switch(sigNo) {
   case VKI_SIGQUIT:	/* core */
   case VKI_SIGILL:	/* core */
   case VKI_SIGABRT:	/* core */
   case VKI_SIGFPE:	/* core */
   case VKI_SIGSEGV:	/* core */
   case VKI_SIGBUS:	/* core */
   case VKI_SIGTRAP:	/* core */
   case VKI_SIGXCPU:	/* core */
   case VKI_SIGXFSZ:	/* core */
      terminate = True;
      core = True;
      break;

   case VKI_SIGHUP:	/* term */
   case VKI_SIGINT:	/* term */
   case VKI_SIGKILL:	/* term - we won't see this */
   case VKI_SIGPIPE:	/* term */
   case VKI_SIGALRM:	/* term */
   case VKI_SIGTERM:	/* term */
   case VKI_SIGUSR1:	/* term */
   case VKI_SIGUSR2:	/* term */
   case VKI_SIGIO:	/* term */
   case VKI_SIGPWR:	/* term */
   case VKI_SIGSYS:	/* term */
   case VKI_SIGPROF:	/* term */
   case VKI_SIGVTALRM:	/* term */
   case VKI_SIGRTMIN ... VKI_SIGRTMAX: /* term */
      terminate = True;
      break;
   }

   vg_assert(!core || (core && terminate));

   if (VG_(clo_trace_signals))
      VG_(message)(Vg_DebugMsg, "delivering %d to default handler %s%s",
		   sigNo, terminate ? "terminate" : "", core ? "+core" : "");

   if (terminate) {
      struct vki_rlimit corelim;
      Bool could_core = core;

      if (core) {
	 /* If they set the core-size limit to zero, don't generate a
	    core file */
	 
	 VG_(getrlimit)(VKI_RLIMIT_CORE, &corelim);

	 if (corelim.rlim_cur == 0)
	    core = False;
      }

      if (VG_(clo_verbosity) != 0 && (could_core || VG_(clo_verbosity) > 1)) {
	 VG_(message)(Vg_UserMsg, "");
	 VG_(message)(Vg_UserMsg, "Process terminating with default action of signal %d (%s)%s", 
		      sigNo, signame(sigNo), core ? ": dumping core" : "");

	 /* Be helpful - decode some more details about this fault */
	 if (info->si_code > VKI_SI_USER) {
	    const Char *event = NULL;

	    switch(sigNo) {
	    case VKI_SIGSEGV:
	       switch(info->si_code) {
	       case 1: event = "Access not within mapped region"; break;
	       case 2: event = "Bad permissions for mapped region"; break;
	       }
	       break;

	    case VKI_SIGILL:
	       switch(info->si_code) {
	       case 1: event = "Illegal opcode"; break;
	       case 2: event = "Illegal operand"; break;
	       case 3: event = "Illegal addressing mode"; break;
	       case 4: event = "Illegal trap"; break;
	       case 5: event = "Privileged opcode"; break;
	       case 6: event = "Privileged register"; break;
	       case 7: event = "Coprocessor error"; break;
	       case 8: event = "Internal stack error"; break;
	       }
	       break;

	    case VKI_SIGFPE:
	       switch (info->si_code) {
	       case 1: event = "Integer divide by zero"; break;
	       case 2: event = "Integer overflow"; break;
	       case 3: event = "FP divide by zero"; break;
	       case 4: event = "FP overflow"; break;
	       case 5: event = "FP underflow"; break;
	       case 6: event = "FP inexact"; break;
	       case 7: event = "FP invalid operation"; break;
	       case 8: event = "FP subscript out of range"; break;
	       }
	       break;

	    case VKI_SIGBUS:
	       switch (info->si_code) {
	       case 1: event = "Invalid address alignment"; break;
	       case 2: event = "Non-existent physical address"; break;
	       case 3: event = "Hardware error"; break;
	       }
	       break;
	    }

	    if (event != NULL)
	       VG_(message)(Vg_UserMsg, " %s at address %p", 
			    event, info->_sifields._sigfault._addr);
	 }

	 if (tid != VG_INVALID_THREADID) {
	    ExeContext *ec = VG_(get_ExeContext)(tid);
	    VG_(pp_ExeContext)(ec);
	 }
      }

      if (VG_(is_action_requested)( "Attach to debugger", & VG_(clo_db_attach) )) {
         VG_(start_debugger)( tid );
      }

      if (core) {
	 static struct vki_rlimit zero = { 0, 0 };

	 make_coredump(tid, info, corelim.rlim_cur);

	 /* make sure we don't get a confusing kernel-generated coredump */
	 VG_(setrlimit)(VKI_RLIMIT_CORE, &zero);
      }

      if (VG_(fatal_signal_set)) {
	 VG_(fatal_sigNo) = sigNo;
	 __builtin_longjmp(VG_(fatal_signal_jmpbuf), 1);
      }
   }

   VG_(kill_self)(sigNo);

   vg_assert(!terminate);
}

static void synth_fault_common(ThreadId tid, Addr addr, Int si_code)
{
   vki_ksiginfo_t info;

   vg_assert(VG_(threads)[tid].status == VgTs_Runnable);

   info.si_signo = VKI_SIGSEGV;
   info.si_code = si_code;
   info._sifields._sigfault._addr = (void*)addr;

   VG_(resume_scheduler)(VKI_SIGSEGV, &info);
   VG_(deliver_signal)(tid, &info, False);
}

// Synthesize a fault where the address is OK, but the page
// permissions are bad.
void VG_(synth_fault_perms)(ThreadId tid, Addr addr)
{
   synth_fault_common(tid, addr, 2);
}

// Synthesize a fault where the address there's nothing mapped at the address.
void VG_(synth_fault_mapping)(ThreadId tid, Addr addr)
{
   synth_fault_common(tid, addr, 1);
}

// Synthesize a misc memory fault.
void VG_(synth_fault)(ThreadId tid)
{
   synth_fault_common(tid, 0, 0x80);
}

void VG_(deliver_signal) ( ThreadId tid, const vki_ksiginfo_t *info, Bool async )
{
   Int			sigNo = info->si_signo;
   vki_ksigset_t	handlermask;
   SCSS_Per_Signal	*handler = &vg_scss.scss_per_sig[sigNo];
   void			*handler_fn;
   ThreadState		*tst = VG_(get_ThreadState)(tid);

   if (VG_(clo_trace_signals))
      VG_(message)(Vg_DebugMsg,"delivering signal %d (%s) to thread %d", 
		   sigNo, signame(sigNo), tid );

   if (sigNo == VKI_SIGVGINT) {
      /* If this is a SIGVGINT, then we just ACK the signal and carry
	 on; the application need never know about it (except for any
	 effect on its syscalls). */
      vg_assert(async);

      if (tst->status == VgTs_WaitSys) {
	 /* blocked in a syscall; we assume it should be interrupted */
	 if (tst->m_eax == -VKI_ERESTARTSYS)
	    tst->m_eax = -VKI_EINTR;
      }

      VG_(proxy_sigack)(tid, &tst->sig_mask);
      return;
   }

   /* If thread is currently blocked in a syscall, then resume as
      runnable.  If the syscall needs restarting, tweak the machine
      state to make it happen. */
   if (tst->status == VgTs_WaitSys) {
      vg_assert(tst->syscallno != -1);

      /* OK, the thread was waiting for a syscall to complete.  This
	 means that the proxy has either not yet processed the
	 RunSyscall request, or was processing it when the signal
	 came.  Either way, it is going to give us some syscall
	 results right now, so wait for them to appear.  This makes
	 the thread runnable again, so we're in the right state to run
	 the handler.  We ask post_syscall to restart based on the
	 client's sigaction flags. */
      if (0)
	 VG_(printf)("signal %d interrupted syscall %d; restart=%d\n",
		     sigNo, tst->syscallno, !!(handler->scss_flags & VKI_SA_RESTART));
      VG_(proxy_wait_sys)(tid, !!(handler->scss_flags & VKI_SA_RESTART));
   }

   /* If the client specifies SIG_IGN, treat it as SIG_DFL */
   handler_fn = handler->scss_handler;
   if (handler_fn == VKI_SIG_IGN)
      handler_fn = VKI_SIG_DFL;

   vg_assert(handler_fn != VKI_SIG_IGN);

   if (sigNo == VKI_SIGCHLD && (handler->scss_flags & VKI_SA_NOCLDWAIT)) {
      //VG_(printf)("sigNo==SIGCHLD and app asked for NOCLDWAIT\n");
      vg_babyeater(sigNo, NULL, NULL);
   }

   if (handler_fn == VKI_SIG_DFL) {
      handlermask = tst->sig_mask; /* no change to signal mask */
      vg_default_action(info, tid);
   } else {
      /* Create a signal delivery frame, and set the client's %ESP and
	 %EIP so that when execution continues, we will enter the
	 signal handler with the frame on top of the client's stack,
	 as it expects. */
      vg_assert(VG_(is_valid_tid)(tid));
      vg_push_signal_frame ( tid, info );

      if (handler->scss_flags & VKI_SA_ONESHOT) {
	 /* Do the ONESHOT thing. */
	 handler->scss_handler = VKI_SIG_DFL;

	 VG_(handle_SCSS_change)( False /* lazy update */ );
      }
   
      switch(tst->status) {
      case VgTs_Runnable:
	 break;

      case VgTs_WaitSys:
      case VgTs_WaitJoiner:
      case VgTs_WaitJoinee:
      case VgTs_WaitMX:
      case VgTs_WaitCV:
      case VgTs_Sleeping:
	 tst->status = VgTs_Runnable;
	 break;

      case VgTs_Empty:
	 VG_(core_panic)("unexpected thread state");
	 break;
      }

      /* handler gets the union of the signal's mask and the thread's
	 mask */
      handlermask = handler->scss_mask;
      VG_(ksigaddset_from_set)(&handlermask, &VG_(threads)[tid].sig_mask);

      /* also mask this signal, unless they ask us not to */
      if (!(handler->scss_flags & VKI_SA_NOMASK))
	 VG_(ksigaddset)(&handlermask, sigNo);
   }

   /* tell proxy we're about to start running the handler */
   if (async)
      VG_(proxy_sigack)(tid, &handlermask);
}


/* 
   If the client set the handler for SIGCHLD to SIG_IGN, then we need
   to automatically dezombie any dead children.  Also used if the
   client set the SA_NOCLDWAIT on their SIGCHLD handler.
 */
static
void vg_babyeater ( Int sigNo, vki_ksiginfo_t *info, struct vki_ucontext *uc )
{
   Int status;
   Int pid;

   vg_assert(sigNo == VKI_SIGCHLD);

   while((pid = VG_(waitpid)(-1, &status, VKI_WNOHANG)) > 0) {
      if (VG_(clo_trace_signals)) 
	 VG_(message)(Vg_DebugMsg, "babyeater reaped %d", pid);
   }
}

/* 
   Receive an async signal from the host. 

   It being called in the context of a proxy LWP, and therefore is an
   async signal aimed at one of our threads.  In this case, we pass
   the signal info to the main thread with VG_(proxy_handlesig)().

   This should *never* be in the context of the main LWP, because
   all signals for which this is the handler should be blocked there.
*/
static 
void vg_async_signalhandler ( Int sigNo, vki_ksiginfo_t *info, struct vki_ucontext *uc )
{
   if (VG_(gettid)() == VG_(main_pid)) {
      VG_(printf)("got signal %d in LWP %d (%d)\n",
		  sigNo, VG_(gettid)(), VG_(gettid)(), VG_(main_pid));
      vg_assert(VG_(ksigismember)(&uc->uc_sigmask, sigNo));
   }

   vg_assert(VG_(gettid)() != VG_(main_pid));

   VG_(proxy_handlesig)(info, &uc->uc_mcontext);
}

/* 
   Recieve a sync signal from the host. 

   This should always be called from the main thread, though it may be
   called in a proxy LWP if someone sends an async version of one of
   the sync signals.
*/
static
void vg_sync_signalhandler ( Int sigNo, vki_ksiginfo_t *info, struct vki_ucontext *uc )
{
   Int           dummy_local;

   vg_assert(info != NULL);
   vg_assert(info->si_signo == sigNo);
   vg_assert(sigNo == VKI_SIGSEGV ||
	     sigNo == VKI_SIGBUS  ||
	     sigNo == VKI_SIGFPE  ||
	     sigNo == VKI_SIGILL);

   if (VG_(gettid)() != VG_(main_pid)) {
      /* We were sent one of our sync signals in an async way (or the
	 proxy LWP code has a bug) */
      vg_assert(info->si_code <= VKI_SI_USER);

      VG_(proxy_handlesig)(info, &uc->uc_mcontext);
      return;
   }


   /*
   if (sigNo == VKI_SIGUSR1) {
      VG_(printf)("YOWZA!  SIGUSR1\n\n");
      VG_(clo_trace_pthread_level) = 2;
      VG_(clo_trace_sched) = True;
      VG_(clo_trace_syscalls) = True;
      VG_(clo_trace_signals) = True;
      return;
   }
   */

   if (VG_(clo_trace_signals)) {
      VG_(message)(Vg_DebugMsg, "signal %d arrived ... si_code=%d", sigNo, info->si_code );
   }
   vg_assert(sigNo >= 1 && sigNo <= VKI_KNSIG);

   /* Sanity check.  Ensure we're really running on the signal stack
      we asked for. */
   if (!(
         ((Char*)(&(VG_(sigstack)[0])) <= (Char*)(&dummy_local))
         &&
         ((Char*)(&dummy_local) < (Char*)(&(VG_(sigstack)[VG_SIGSTACK_SIZE_W])))
         )
      ) {
     VG_(message)(Vg_DebugMsg, 
        "FATAL: signal delivered on the wrong stack?!");
     VG_(message)(Vg_DebugMsg, 
        "A possible workaround follows.  Please tell me");
     VG_(message)(Vg_DebugMsg, 
        "(jseward@acm.org) if the suggested workaround doesn't help.");
     VG_(unimplemented)
        ("support for progs compiled with -p/-pg; "
         "rebuild your prog without -p/-pg");
   }

   vg_assert((Char*)(&(VG_(sigstack)[0])) <= (Char*)(&dummy_local));
   vg_assert((Char*)(&dummy_local) < (Char*)(&(VG_(sigstack)[VG_SIGSTACK_SIZE_W])));

   /* Special fault-handling case. We can now get signals which can
      act upon and immediately restart the faulting instruction.
    */
   if (info->si_signo == VKI_SIGSEGV) {
      ThreadId tid = VG_(get_current_or_recent_tid)();
      Addr fault = (Addr)info->_sifields._sigfault._addr;
      Addr esp = VG_(is_running_thread)(tid) ?
	 VG_(baseBlock)[VGOFF_(m_esp)] : VG_(threads)[tid].m_esp;
      Segment *seg;

      seg = VG_(find_segment)(fault);
      if (seg != NULL)
	 seg = VG_(next_segment)(seg);
      else 
         seg = VG_(first_segment)();

      if (VG_(clo_trace_signals)) {
	 if (seg == NULL)
	    VG_(message)(Vg_DebugMsg,
			 "SIGSEGV: si_code=%d faultaddr=%p tid=%d esp=%p seg=NULL shad=%p-%p",
			 info->si_code, fault, tid, esp,
			 VG_(shadow_base), VG_(shadow_end));
	 else
	    VG_(message)(Vg_DebugMsg,
			 "SIGSEGV: si_code=%d faultaddr=%p tid=%d esp=%p seg=%p-%p fl=%x shad=%p-%p",
			 info->si_code, fault, tid, esp, seg->addr, seg->addr+seg->len, seg->flags,
			 VG_(shadow_base), VG_(shadow_end));
      }

      if (info->si_code == 1		&&	/* SEGV_MAPERR */
	  seg != NULL                   &&
	  fault >= esp			&&
	  fault < seg->addr		&&
	  (seg->flags & SF_GROWDOWN)) {
	 /* If the fault address is above esp but below the current known
	    stack segment base, and it was a fault because there was
	    nothing mapped there (as opposed to a permissions fault),
	    then extend the stack segment. 
	 */
	 Addr base = PGROUNDDN(esp);
         if ((void*)-1 != VG_(mmap)((Char *)base, seg->addr - base,
                              VKI_PROT_READ|VKI_PROT_WRITE|VKI_PROT_EXEC,
                              VKI_MAP_PRIVATE|VKI_MAP_FIXED|VKI_MAP_ANONYMOUS|VKI_MAP_CLIENT,
                              SF_STACK|SF_GROWDOWN,
                              -1, 0))
         {
           return;             // extension succeeded, restart instruction
	 }
	 /* Otherwise fall into normal signal handling */
      } else if (info->si_code == 2 && /* SEGV_ACCERR */
		 VG_(needs).shadow_memory &&
		 VG_(is_shadow_addr)(fault)) {
	 /* If there's a fault within the shadow memory range, and it
	    is a permissions fault, then it means that the client is
	    using some memory which had not previously been used.
	    This catches those faults, makes the memory accessible,
	    and calls the tool to initialize that page.
	 */
	 static Int recursion = 0;

	 if (recursion++ == 0) {
	    VG_(init_shadow_range)(PGROUNDDN(fault), VKI_BYTES_PER_PAGE, True);
	    recursion--;
	    return;
	 } else {
	    /* otherwise fall into normal SEGV handling */	    
	    recursion--;
	 }
      }
   }

   /* Can't continue; must longjmp back to the scheduler and thus
      enter the sighandler immediately. */
   VG_(resume_scheduler)(sigNo, info);

   if (info->si_code <= VKI_SI_USER) {
      /* 
	 OK, one of sync signals was sent from user-mode, so try to
	 deliver it to someone who cares.  Just add it to the
	 process-wide pending signal set - signal routing will deliver
	 it to someone eventually.

	 The only other place which touches proc_pending is
	 VG_(route_signals), and it has signals blocked while doing
	 so, so there's no race.
      */
      VG_(message)(Vg_DebugMsg, 
		   "adding signal %d to pending set", sigNo);
      VG_(ksigaddset)(&proc_pending, sigNo);
   } else {
      /* 
	 A bad signal came from the kernel (indicating an instruction
	 generated it), but there was no jumpbuf set up.  This means
	 it was actually generated by Valgrind internally.
       */
      struct vki_sigcontext *sc = &uc->uc_mcontext;
      Char buf[1024];

      VG_(message)(Vg_DebugMsg, 
		   "INTERNAL ERROR: Valgrind received a signal %d (%s) - exiting",
		   sigNo, signame(sigNo));

      buf[0] = 0;
      if (1 && !VG_(get_fnname)(sc->eip, buf+2, sizeof(buf)-5)) {
	 Int len;

	 buf[0] = ' ';
	 buf[1] = '(';
	 len = VG_(strlen)(buf);
	 buf[len] = ')';
	 buf[len+1] = '\0';
      }

      VG_(message)(Vg_DebugMsg, 
		   "si_code=%x Fault EIP: %p%s; Faulting address: %p",
		   info->si_code, sc->eip, buf, info->_sifields._sigfault._addr);

      if (0)
	 VG_(kill_self)(sigNo);		/* generate a core dump */
      VG_(core_panic)("Killed by fatal signal");
   }
}


/* 
   This signal handler exists only so that the scheduler thread can
   poke the LWP to make it fall out of whatever syscall it is in.
   Used for thread termination and cancellation.
 */
static void proxy_sigvg_handler(int signo, vki_ksiginfo_t *si, struct vki_ucontext *uc)
{
   vg_assert(signo == VKI_SIGVGINT || signo == VKI_SIGVGKILL);
   vg_assert(si->si_signo == signo);

   /* only pay attention to it if it came from the scheduler */
   if (si->si_code == VKI_SI_TKILL &&
       si->_sifields._kill._pid == VG_(main_pid)) {
      vg_assert(si->si_code == VKI_SI_TKILL);
      vg_assert(si->_sifields._kill._pid == VG_(main_pid));
   
      VG_(proxy_handlesig)(si, &uc->uc_mcontext);
   }
}


/* The outer insn loop calls here to reenable a host signal if
   vg_oursighandler longjmp'd.
*/
void VG_(unblock_host_signal) ( Int sigNo )
{
   vg_assert(sigNo == VKI_SIGSEGV ||
	     sigNo == VKI_SIGBUS ||
	     sigNo == VKI_SIGILL ||
	     sigNo == VKI_SIGFPE);
   set_main_sigmask();
}


static __attribute((unused))
void pp_vg_ksigaction ( vki_ksigaction* sa )
{
   Int i;
   VG_(printf)("vg_ksigaction: handler %p, flags 0x%x, restorer %p\n", 
               sa->ksa_handler, (UInt)sa->ksa_flags, sa->ksa_restorer);
   VG_(printf)("vg_ksigaction: { ");
   for (i = 1; i <= VKI_KNSIG; i++)
      if (VG_(ksigismember(&(sa->ksa_mask),i)))
         VG_(printf)("%d ", i);
   VG_(printf)("}\n");
}

/* 
   In pre-2.6 kernels, the kernel didn't distribute signals to threads
   in a thread-group properly, so we need to do it here.
 */
void VG_(route_signals)(void)
{
   static const struct vki_timespec zero = { 0, 0 };
   static ThreadId start_tid = 1;	/* tid to start scanning from */
   vki_ksigset_t set;
   vki_ksiginfo_t si;
   Int sigNo;

   vg_assert(VG_(gettid)() == VG_(main_pid));
   vg_assert(is_correct_sigmask());

   if (!VG_(do_signal_routing))
      return;

   /* get the scheduler LWP's signal mask, and use it as the set of
      signals we're polling for - also block all signals to prevent
      races */
   VG_(block_all_host_signals) ( &set );

   /* grab any pending signals and add them to the pending signal set */
   while(VG_(ksigtimedwait)(&set, &si, &zero) > 0)
      VG_(ksigaddset)(&proc_pending, si.si_signo);

   /* transfer signals from the process pending set to a particular
      thread which has it unblocked */
   for(sigNo = 0; sigNo < VKI_KNSIG; sigNo++) {
      ThreadId tid;
      ThreadId end_tid;
      Int target = -1;
      
      if (!VG_(ksigismember)(&proc_pending, sigNo))
	 continue;

      end_tid = start_tid - 1;
      if (end_tid < 0 || end_tid >= VG_N_THREADS)
	      end_tid = VG_N_THREADS-1;

      /* look for a suitable thread to deliver it to */
      for(tid = start_tid;
	  tid != end_tid;
	  tid = (tid + 1) % VG_N_THREADS) {
	 ThreadState *tst = &VG_(threads)[tid];

	 if (tst->status == VgTs_Empty)
	    continue;

	 if (!VG_(ksigismember)(&tst->sig_mask, sigNo)) {
	    vg_assert(tst->proxy != NULL);
	    target = tid;
	    start_tid = tid;
	    break;
	 }
      }
      
      /* found one - deliver it and be done */
      if (target != -1) {
	 if (VG_(clo_trace_signals))
	    VG_(message)(Vg_DebugMsg, "Routing signal %d to tid %d",
			 sigNo, tid);
	 VG_(proxy_sendsig)(target, sigNo);
	 VG_(ksigdelset)(&proc_pending, sigNo);
      }
   }

   /* restore signal mask */
   VG_(restore_all_host_signals) (&set);
}

/* At startup, copy the process' real signal state to the SCSS.
   Whilst doing this, block all real signals.  Then calculate SKSS and
   set the kernel to that.  Also initialise DCSS. 
*/
void VG_(sigstartup_actions) ( void )
{
   Int i, ret;
   vki_ksigset_t  saved_procmask;
   vki_kstack_t   altstack_info;
   vki_ksigaction sa;

   /* VG_(printf)("SIGSTARTUP\n"); */
   /* Block all signals.  saved_procmask remembers the previous mask,
      which the first thread inherits.
   */
   VG_(block_all_host_signals)( &saved_procmask );

   /* clear process-wide pending signal set */
   VG_(ksigemptyset)(&proc_pending);

   /* Set the signal mask which the scheduler LWP should maintain from
      now on. */
   set_main_sigmask();

   /* Copy per-signal settings to SCSS. */
   for (i = 1; i <= VKI_KNSIG; i++) {

      /* Get the old host action */
      ret = VG_(ksigaction)(i, NULL, &sa);
      vg_assert(ret == 0);

      if (VG_(clo_trace_signals))
         VG_(printf)("snaffling handler 0x%x for signal %d\n", 
                     (Addr)(sa.ksa_handler), i );

      vg_scss.scss_per_sig[i].scss_handler  = sa.ksa_handler;
      vg_scss.scss_per_sig[i].scss_flags    = sa.ksa_flags;
      vg_scss.scss_per_sig[i].scss_mask     = sa.ksa_mask;
      vg_scss.scss_per_sig[i].scss_restorer = sa.ksa_restorer;
   }

   /* Our private internal signals are treated as ignored */
   vg_scss.scss_per_sig[VKI_SIGVGINT].scss_handler = VKI_SIG_IGN;
   vg_scss.scss_per_sig[VKI_SIGVGINT].scss_flags = VKI_SA_SIGINFO;
   VG_(ksigfillset)(&vg_scss.scss_per_sig[VKI_SIGVGINT].scss_mask);
   vg_scss.scss_per_sig[VKI_SIGVGKILL].scss_handler = VKI_SIG_IGN;
   vg_scss.scss_per_sig[VKI_SIGVGKILL].scss_flags = VKI_SA_SIGINFO;
   VG_(ksigfillset)(&vg_scss.scss_per_sig[VKI_SIGVGKILL].scss_mask);

   /* Copy the process' signal mask into the root thread. */
   vg_assert(VG_(threads)[1].status == VgTs_Runnable);
   VG_(threads)[1].sig_mask = saved_procmask;
   VG_(proxy_setsigmask)(1);

   /* Register an alternative stack for our own signal handler to run on. */
   altstack_info.ss_sp = &(VG_(sigstack)[0]);
   altstack_info.ss_size = VG_SIGSTACK_SIZE_W * sizeof(UInt);
   altstack_info.ss_flags = 0;
   ret = VG_(ksigaltstack)(&altstack_info, NULL);
   if (ret != 0) {
      VG_(core_panic)(
         "vg_sigstartup_actions: couldn't install alternative sigstack");
   }
   if (VG_(clo_trace_signals)) {
      VG_(message)(Vg_DebugExtraMsg, 
         "vg_sigstartup_actions: sigstack installed ok");
   }

   /* DEBUGGING HACK */
   /* VG_(ksignal)(VKI_SIGUSR1, &VG_(oursignalhandler)); */

   /* Calculate SKSS and apply it.  This also sets the initial kernel
      mask we need to run with. */
   VG_(handle_SCSS_change)( True /* forced update */ );

}


/* Copy the process' sim signal state to the real state,
   for when we transfer from the simulated to real CPU.
   PROBLEM: what if we're running a signal handler when we
   get here?  Hmm.
   I guess we wind up in vg_signalreturn_bogusRA, *or* the
   handler has done/will do a longjmp, in which case we're ok.
*/
void VG_(sigshutdown_actions) ( void )
{
   Int i, ret;

   vki_ksigset_t  saved_procmask;
   vki_ksigaction sa;

   VG_(block_all_host_signals)( &saved_procmask );

   /* Copy per-signal settings from SCSS. */
   for (i = 1; i <= VKI_KNSIG; i++) {

      sa.ksa_handler  = vg_scss.scss_per_sig[i].scss_handler;
      sa.ksa_flags    = vg_scss.scss_per_sig[i].scss_flags;
      sa.ksa_mask     = vg_scss.scss_per_sig[i].scss_mask;
      sa.ksa_restorer = vg_scss.scss_per_sig[i].scss_restorer;

      if (VG_(clo_trace_signals))
         VG_(printf)("restoring handler 0x%x for signal %d\n", 
                     (Addr)(sa.ksa_handler), i );

      /* Set the old host action */
      ret = VG_(ksigaction)(i, &sa, NULL);
      if (i != VKI_SIGKILL && i != VKI_SIGSTOP) 
         vg_assert(ret == 0);
   }

   /* Restore the sig alt stack. */
   ret = VG_(ksigaltstack)(&VG_(threads)[1].altstack, NULL);
   vg_assert(ret == 0);

   /* A bit of a kludge -- set the sigmask to that of the root
      thread. */
   vg_assert(VG_(threads)[1].status != VgTs_Empty);
   VG_(restore_all_host_signals)( &VG_(threads)[1].sig_mask );
}


/*--------------------------------------------------------------------*/
/*--- end                                             vg_signals.c ---*/
/*--------------------------------------------------------------------*/

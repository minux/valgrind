
/*--------------------------------------------------------------------*/
/*--- A user-space pthreads implementation.         vg_scheduler.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, an extensible x86 protected-mode
   emulator for monitoring program execution on x86-Unixes.

   Copyright (C) 2000-2003 Julian Seward 
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

#include "valgrind.h" /* for VG_USERREQ__RUNNING_ON_VALGRIND and
                             VG_USERREQ__DISCARD_TRANSLATIONS, and others */
#include "vg_include.h"

/* BORKAGE/ISSUES as of 29 May 02

- Currently, when a signal is run, just the ThreadStatus.status fields 
  are saved in the signal frame, along with the CPU state.  Question: 
  should I also save and restore:
     ThreadStatus.joiner 
     ThreadStatus.waited_on_mid
     ThreadStatus.awaken_at
     ThreadStatus.retval
  Currently unsure, and so am not doing so.

- So, what's the deal with signals and mutexes?  If a thread is
  blocked on a mutex, or for a condition variable for that matter, can
  signals still be delivered to it?  This has serious consequences --
  deadlocks, etc.

  TODO for valgrind-1.0:

- Update assertion checking in scheduler_sanity().

  TODO sometime:

- Mutex scrubbing - clearup_after_thread_exit: look for threads
  blocked on mutexes held by the exiting thread, and release them
  appropriately. (??)

*/


/* ---------------------------------------------------------------------
   Types and globals for the scheduler.
   ------------------------------------------------------------------ */

/* type ThreadId is defined in vg_include.h. */

/* struct ThreadState is defined in vg_include.h. */

/* Globals.  A statically allocated array of threads.  NOTE: [0] is
   never used, to simplify the simulation of initialisers for
   LinuxThreads. */
ThreadState VG_(threads)[VG_N_THREADS];

/* The process' fork-handler stack. */
static Int              vg_fhstack_used = 0;
static ForkHandlerEntry vg_fhstack[VG_N_FORKHANDLERSTACK];


/* The tid of the thread currently in VG_(baseBlock). */
static ThreadId vg_tid_currently_in_baseBlock = VG_INVALID_THREADID;

/* The tid either currently in baseBlock, or was in baseBlock before
   was saved it out; this is only updated when a new thread is loaded
   into the baseBlock */
static ThreadId vg_tid_last_in_baseBlock = VG_INVALID_THREADID;

/* vg_oursignalhandler() might longjmp().  Here's the jmp_buf. */
jmp_buf VG_(scheduler_jmpbuf);
/* This says whether scheduler_jmpbuf is actually valid.  Needed so
   that our signal handler doesn't longjmp when the buffer isn't
   actually valid. */
Bool    VG_(scheduler_jmpbuf_valid) = False;
/* ... and if so, here's the signal which caused it to do so. */
Int     VG_(longjmpd_on_signal);
/* If the current thread gets a syncronous unresumable signal, then
   its details are placed here by the signal handler, to be passed to
   the applications signal handler later on. */
vki_ksiginfo_t VG_(unresumable_siginfo);

/* If != VG_INVALID_THREADID, this is the preferred tid to schedule */
static ThreadId prefer_sched = VG_INVALID_THREADID;

/* Keeping track of keys. */
typedef
   struct {
      /* Has this key been allocated ? */
      Bool inuse;
      /* If .inuse==True, records the address of the associated
         destructor, or NULL if none. */
      void (*destructor)(void*);
   }
   ThreadKeyState;

/* And our array of thread keys. */
static ThreadKeyState vg_thread_keys[VG_N_THREAD_KEYS];

typedef UInt ThreadKey;


UInt VG_(syscall_altered_shadow_reg);
UInt VG_(signal_delivery_altered_shadow_reg);
UInt VG_(pthread_op_altered_shadow_reg);
UInt VG_(client_request_altered_shadow_reg);

/* Forwards */
static void do_client_request ( ThreadId tid );
static void scheduler_sanity ( void );
static void do_pthread_cond_timedwait_TIMEOUT ( ThreadId tid );

/* ---------------------------------------------------------------------
   Helper functions for the scheduler.
   ------------------------------------------------------------------ */

__inline__
Bool VG_(is_valid_tid) ( ThreadId tid )
{
   /* tid is unsigned, hence no < 0 test. */
   if (tid == 0) return False;
   if (tid >= VG_N_THREADS) return False;
   if (VG_(threads)[tid].status == VgTs_Empty) return False;
   return True;
}


__inline__
Bool VG_(is_valid_or_empty_tid) ( ThreadId tid )
{
   /* tid is unsigned, hence no < 0 test. */
   if (tid == 0) return False;
   if (tid >= VG_N_THREADS) return False;
   return True;
}


/* For constructing error messages only: try and identify a thread
   whose stack satisfies the predicate p, or return VG_INVALID_THREADID
   if none do.  A small complication is dealing with any currently
   VG_(baseBlock)-resident thread. 
*/
ThreadId VG_(first_matching_thread_stack)
              ( Bool (*p) ( Addr stack_min, Addr stack_max ))
{
   ThreadId tid, tid_to_skip;

   tid_to_skip = VG_INVALID_THREADID;

   /* First check to see if there's a currently-loaded thread in
      VG_(baseBlock). */
   if (vg_tid_currently_in_baseBlock != VG_INVALID_THREADID) {
      tid = vg_tid_currently_in_baseBlock;
      if ( p ( VG_(baseBlock)[VGOFF_(m_esp)], 
               VG_(threads)[tid].stack_highest_word) )
         return tid;
      else
         tid_to_skip = tid;
   }

   for (tid = 1; tid < VG_N_THREADS; tid++) {
      if (VG_(threads)[tid].status == VgTs_Empty) continue;
      if (tid == tid_to_skip) continue;
      if ( p ( VG_(threads)[tid].m_esp,
               VG_(threads)[tid].stack_highest_word) )
         return tid;
   }
   return VG_INVALID_THREADID;
}
 

/* Print the scheduler status. */
void VG_(pp_sched_status) ( void )
{
   Int i; 
   VG_(printf)("\nsched status:\n"); 
   for (i = 1; i < VG_N_THREADS; i++) {
      if (VG_(threads)[i].status == VgTs_Empty) continue;
      VG_(printf)("\nThread %d: status = ", i);
      switch (VG_(threads)[i].status) {
         case VgTs_Runnable:   VG_(printf)("Runnable"); break;
         case VgTs_WaitJoinee: VG_(printf)("WaitJoinee(%d)", 
                                           VG_(threads)[i].joiner_jee_tid);
                               break;
         case VgTs_WaitJoiner: VG_(printf)("WaitJoiner"); break;
         case VgTs_Sleeping:   VG_(printf)("Sleeping"); break;
         case VgTs_WaitMX:     VG_(printf)("WaitMX"); break;
         case VgTs_WaitCV:     VG_(printf)("WaitCV"); break;
         case VgTs_WaitSys:    VG_(printf)("WaitSys"); break;
         default: VG_(printf)("???"); break;
      }
      VG_(printf)(", associated_mx = %p, associated_cv = %p\n", 
                  VG_(threads)[i].associated_mx,
                  VG_(threads)[i].associated_cv );
      VG_(pp_ExeContext)( 
         VG_(get_ExeContext2)( VG_(threads)[i].m_eip, VG_(threads)[i].m_ebp,
                               VG_(threads)[i].m_esp, 
                               VG_(threads)[i].stack_highest_word)
      );
   }
   VG_(printf)("\n");
}



static
void print_sched_event ( ThreadId tid, Char* what )
{
   VG_(message)(Vg_DebugMsg, "  SCHED[%d]: %s", tid, what );
}


static
void print_pthread_event ( ThreadId tid, Char* what )
{
   VG_(message)(Vg_DebugMsg, "PTHREAD[%d]: %s", tid, what );
}


static
Char* name_of_sched_event ( UInt event )
{
   switch (event) {
      case VG_TRC_EBP_JMP_SYSCALL:    return "SYSCALL";
      case VG_TRC_EBP_JMP_CLIENTREQ:  return "CLIENTREQ";
      case VG_TRC_INNER_COUNTERZERO:  return "COUNTERZERO";
      case VG_TRC_INNER_FASTMISS:     return "FASTMISS";
      case VG_TRC_UNRESUMABLE_SIGNAL: return "FATALSIGNAL";
      default:                        return "??UNKNOWN??";
  }
}


/* Create a translation of the client basic block beginning at
   orig_addr, and add it to the translation cache & translation table.
   This probably doesn't really belong here, but, hey ... 
*/
static
void create_translation_for ( ThreadId tid, Addr orig_addr )
{
   Addr   trans_addr;
   Int    orig_size, trans_size;
   UShort jumps[VG_MAX_JUMPS];
   Int    i;

   for(i = 0; i < VG_MAX_JUMPS; i++)
      jumps[i] = (UShort)-1;

   /* Make a translation, into temporary storage. */
   VG_(translate)( tid, orig_addr,                                /* in */
                   &orig_size, &trans_addr, &trans_size, jumps ); /* out */

   /* Copy data at trans_addr into the translation cache. */
   /* Since the .orig_size and .trans_size fields are
      UShort, be paranoid. */
   vg_assert(orig_size > 0 && orig_size < 65536);
   vg_assert(trans_size > 0 && trans_size < 65536);

   VG_(add_to_trans_tab)( orig_addr, orig_size, trans_addr, trans_size, jumps );

   /* Free the intermediary -- was allocated by VG_(emit_code). */
   VG_(arena_free)( VG_AR_JITTER, (void*)trans_addr );
}


/* Allocate a completely empty ThreadState record. */
static
ThreadId vg_alloc_ThreadState ( void )
{
   Int i;
   for (i = 1; i < VG_N_THREADS; i++) {
      if (VG_(threads)[i].status == VgTs_Empty)
         return i;
   }
   VG_(printf)("vg_alloc_ThreadState: no free slots available\n");
   VG_(printf)("Increase VG_N_THREADS, rebuild and try again.\n");
   VG_(core_panic)("VG_N_THREADS is too low");
   /*NOTREACHED*/
}

ThreadState *VG_(get_ThreadState)(ThreadId tid)
{
   vg_assert(tid >= 0 && tid < VG_N_THREADS);
   return &VG_(threads)[tid];
}

Bool VG_(is_running_thread)(ThreadId tid)
{
   ThreadId curr = VG_(get_current_tid)();
   return (curr == tid && VG_INVALID_THREADID != tid);
}

ThreadId VG_(get_current_tid) ( void )
{
   if (!VG_(is_valid_tid)(vg_tid_currently_in_baseBlock))
      return VG_INVALID_THREADID;
   return vg_tid_currently_in_baseBlock;
}

ThreadId VG_(get_current_or_recent_tid) ( void )
{
   vg_assert(vg_tid_currently_in_baseBlock == vg_tid_last_in_baseBlock ||
	     vg_tid_currently_in_baseBlock == VG_INVALID_THREADID);
   vg_assert(VG_(is_valid_tid)(vg_tid_last_in_baseBlock));

   return vg_tid_last_in_baseBlock;
}

/* Copy the saved state of a thread into VG_(baseBlock), ready for it
   to be run. */
void VG_(load_thread_state) ( ThreadId tid )
{
   Int i;
   vg_assert(vg_tid_currently_in_baseBlock == VG_INVALID_THREADID);

   VG_(baseBlock)[VGOFF_(ldt)]  = (UInt)VG_(threads)[tid].ldt;
   VG_(baseBlock)[VGOFF_(m_cs)] = VG_(threads)[tid].m_cs;
   VG_(baseBlock)[VGOFF_(m_ss)] = VG_(threads)[tid].m_ss;
   VG_(baseBlock)[VGOFF_(m_ds)] = VG_(threads)[tid].m_ds;
   VG_(baseBlock)[VGOFF_(m_es)] = VG_(threads)[tid].m_es;
   VG_(baseBlock)[VGOFF_(m_fs)] = VG_(threads)[tid].m_fs;
   VG_(baseBlock)[VGOFF_(m_gs)] = VG_(threads)[tid].m_gs;

   VG_(baseBlock)[VGOFF_(m_eax)] = VG_(threads)[tid].m_eax;
   VG_(baseBlock)[VGOFF_(m_ebx)] = VG_(threads)[tid].m_ebx;
   VG_(baseBlock)[VGOFF_(m_ecx)] = VG_(threads)[tid].m_ecx;
   VG_(baseBlock)[VGOFF_(m_edx)] = VG_(threads)[tid].m_edx;
   VG_(baseBlock)[VGOFF_(m_esi)] = VG_(threads)[tid].m_esi;
   VG_(baseBlock)[VGOFF_(m_edi)] = VG_(threads)[tid].m_edi;
   VG_(baseBlock)[VGOFF_(m_ebp)] = VG_(threads)[tid].m_ebp;
   VG_(baseBlock)[VGOFF_(m_esp)] = VG_(threads)[tid].m_esp;
   VG_(baseBlock)[VGOFF_(m_eflags)] 
      = VG_(threads)[tid].m_eflags & ~EFlagD;
   VG_(baseBlock)[VGOFF_(m_dflag)] 
      = VG_(extractDflag)(VG_(threads)[tid].m_eflags);
   VG_(baseBlock)[VGOFF_(m_eip)] = VG_(threads)[tid].m_eip;

   for (i = 0; i < VG_SIZE_OF_SSESTATE_W; i++)
      VG_(baseBlock)[VGOFF_(m_ssestate) + i] 
         = VG_(threads)[tid].m_sse[i];

   if (VG_(needs).shadow_regs) {
      VG_(baseBlock)[VGOFF_(sh_eax)] = VG_(threads)[tid].sh_eax;
      VG_(baseBlock)[VGOFF_(sh_ebx)] = VG_(threads)[tid].sh_ebx;
      VG_(baseBlock)[VGOFF_(sh_ecx)] = VG_(threads)[tid].sh_ecx;
      VG_(baseBlock)[VGOFF_(sh_edx)] = VG_(threads)[tid].sh_edx;
      VG_(baseBlock)[VGOFF_(sh_esi)] = VG_(threads)[tid].sh_esi;
      VG_(baseBlock)[VGOFF_(sh_edi)] = VG_(threads)[tid].sh_edi;
      VG_(baseBlock)[VGOFF_(sh_ebp)] = VG_(threads)[tid].sh_ebp;
      VG_(baseBlock)[VGOFF_(sh_esp)] = VG_(threads)[tid].sh_esp;
      VG_(baseBlock)[VGOFF_(sh_eflags)] = VG_(threads)[tid].sh_eflags;
   } else {
      /* Fields shouldn't be used -- check their values haven't changed. */
      vg_assert(
         VG_UNUSED_SHADOW_REG_VALUE == VG_(threads)[tid].sh_eax &&
         VG_UNUSED_SHADOW_REG_VALUE == VG_(threads)[tid].sh_ebx &&
         VG_UNUSED_SHADOW_REG_VALUE == VG_(threads)[tid].sh_ecx &&
         VG_UNUSED_SHADOW_REG_VALUE == VG_(threads)[tid].sh_edx &&
         VG_UNUSED_SHADOW_REG_VALUE == VG_(threads)[tid].sh_esi &&
         VG_UNUSED_SHADOW_REG_VALUE == VG_(threads)[tid].sh_edi &&
         VG_UNUSED_SHADOW_REG_VALUE == VG_(threads)[tid].sh_ebp &&
         VG_UNUSED_SHADOW_REG_VALUE == VG_(threads)[tid].sh_esp &&
         VG_UNUSED_SHADOW_REG_VALUE == VG_(threads)[tid].sh_eflags);
   }

   vg_tid_currently_in_baseBlock = tid;
   vg_tid_last_in_baseBlock = tid;
}


/* Copy the state of a thread from VG_(baseBlock), presumably after it
   has been descheduled.  For sanity-check purposes, fill the vacated
   VG_(baseBlock) with garbage so as to make the system more likely to
   fail quickly if we erroneously continue to poke around inside
   VG_(baseBlock) without first doing a load_thread_state().  
*/
void VG_(save_thread_state) ( ThreadId tid )
{
   Int i;
   const UInt junk = 0xDEADBEEF;

   vg_assert(vg_tid_currently_in_baseBlock != VG_INVALID_THREADID);


   /* We don't copy out the LDT entry, because it can never be changed
      by the normal actions of the thread, only by the modify_ldt
      syscall, in which case we will correctly be updating
      VG_(threads)[tid].ldt.  This printf happens iff the following
      assertion fails. */
   if ((void*)VG_(threads)[tid].ldt != (void*)VG_(baseBlock)[VGOFF_(ldt)])
      VG_(printf)("VG_(threads)[%d].ldt=%p  VG_(baseBlock)[VGOFF_(ldt)]=%p\n",
		  tid, (void*)VG_(threads)[tid].ldt, 
                       (void*)VG_(baseBlock)[VGOFF_(ldt)]);

   vg_assert((void*)VG_(threads)[tid].ldt 
             == (void*)VG_(baseBlock)[VGOFF_(ldt)]);

   VG_(threads)[tid].m_cs = VG_(baseBlock)[VGOFF_(m_cs)];
   VG_(threads)[tid].m_ss = VG_(baseBlock)[VGOFF_(m_ss)];
   VG_(threads)[tid].m_ds = VG_(baseBlock)[VGOFF_(m_ds)];
   VG_(threads)[tid].m_es = VG_(baseBlock)[VGOFF_(m_es)];
   VG_(threads)[tid].m_fs = VG_(baseBlock)[VGOFF_(m_fs)];
   VG_(threads)[tid].m_gs = VG_(baseBlock)[VGOFF_(m_gs)];

   VG_(threads)[tid].m_eax = VG_(baseBlock)[VGOFF_(m_eax)];
   VG_(threads)[tid].m_ebx = VG_(baseBlock)[VGOFF_(m_ebx)];
   VG_(threads)[tid].m_ecx = VG_(baseBlock)[VGOFF_(m_ecx)];
   VG_(threads)[tid].m_edx = VG_(baseBlock)[VGOFF_(m_edx)];
   VG_(threads)[tid].m_esi = VG_(baseBlock)[VGOFF_(m_esi)];
   VG_(threads)[tid].m_edi = VG_(baseBlock)[VGOFF_(m_edi)];
   VG_(threads)[tid].m_ebp = VG_(baseBlock)[VGOFF_(m_ebp)];
   VG_(threads)[tid].m_esp = VG_(baseBlock)[VGOFF_(m_esp)];
   VG_(threads)[tid].m_eflags 
      = VG_(insertDflag)(VG_(baseBlock)[VGOFF_(m_eflags)],
                         VG_(baseBlock)[VGOFF_(m_dflag)]);
   VG_(threads)[tid].m_eip = VG_(baseBlock)[VGOFF_(m_eip)];

   for (i = 0; i < VG_SIZE_OF_SSESTATE_W; i++)
      VG_(threads)[tid].m_sse[i] 
         = VG_(baseBlock)[VGOFF_(m_ssestate) + i];

   if (VG_(needs).shadow_regs) {
      VG_(threads)[tid].sh_eax = VG_(baseBlock)[VGOFF_(sh_eax)];
      VG_(threads)[tid].sh_ebx = VG_(baseBlock)[VGOFF_(sh_ebx)];
      VG_(threads)[tid].sh_ecx = VG_(baseBlock)[VGOFF_(sh_ecx)];
      VG_(threads)[tid].sh_edx = VG_(baseBlock)[VGOFF_(sh_edx)];
      VG_(threads)[tid].sh_esi = VG_(baseBlock)[VGOFF_(sh_esi)];
      VG_(threads)[tid].sh_edi = VG_(baseBlock)[VGOFF_(sh_edi)];
      VG_(threads)[tid].sh_ebp = VG_(baseBlock)[VGOFF_(sh_ebp)];
      VG_(threads)[tid].sh_esp = VG_(baseBlock)[VGOFF_(sh_esp)];
      VG_(threads)[tid].sh_eflags = VG_(baseBlock)[VGOFF_(sh_eflags)];
   } else {
      /* Fill with recognisable junk */
      VG_(threads)[tid].sh_eax =
      VG_(threads)[tid].sh_ebx =
      VG_(threads)[tid].sh_ecx =
      VG_(threads)[tid].sh_edx =
      VG_(threads)[tid].sh_esi =
      VG_(threads)[tid].sh_edi =
      VG_(threads)[tid].sh_ebp =
      VG_(threads)[tid].sh_esp = 
      VG_(threads)[tid].sh_eflags = VG_UNUSED_SHADOW_REG_VALUE;
   }

   /* Fill it up with junk. */
   VG_(baseBlock)[VGOFF_(ldt)] = junk;
   VG_(baseBlock)[VGOFF_(m_cs)] = junk;
   VG_(baseBlock)[VGOFF_(m_ss)] = junk;
   VG_(baseBlock)[VGOFF_(m_ds)] = junk;
   VG_(baseBlock)[VGOFF_(m_es)] = junk;
   VG_(baseBlock)[VGOFF_(m_fs)] = junk;
   VG_(baseBlock)[VGOFF_(m_gs)] = junk;

   VG_(baseBlock)[VGOFF_(m_eax)] = junk;
   VG_(baseBlock)[VGOFF_(m_ebx)] = junk;
   VG_(baseBlock)[VGOFF_(m_ecx)] = junk;
   VG_(baseBlock)[VGOFF_(m_edx)] = junk;
   VG_(baseBlock)[VGOFF_(m_esi)] = junk;
   VG_(baseBlock)[VGOFF_(m_edi)] = junk;
   VG_(baseBlock)[VGOFF_(m_ebp)] = junk;
   VG_(baseBlock)[VGOFF_(m_esp)] = junk;
   VG_(baseBlock)[VGOFF_(m_eflags)] = junk;
   VG_(baseBlock)[VGOFF_(m_eip)] = junk;

   for (i = 0; i < VG_SIZE_OF_SSESTATE_W; i++)
      VG_(baseBlock)[VGOFF_(m_ssestate) + i] = junk;

   vg_tid_currently_in_baseBlock = VG_INVALID_THREADID;
}


/* Run the thread tid for a while, and return a VG_TRC_* value to the
   scheduler indicating what happened. */
static
UInt run_thread_for_a_while ( ThreadId tid )
{
   volatile UInt trc = 0;
   vg_assert(VG_(is_valid_tid)(tid));
   vg_assert(VG_(threads)[tid].status == VgTs_Runnable);
   vg_assert(VG_(bbs_to_go) > 0);
   vg_assert(!VG_(scheduler_jmpbuf_valid));

   VGP_PUSHCC(VgpRun);
   VG_(load_thread_state) ( tid );

   /* there should be no undealt-with signals */
   vg_assert(VG_(unresumable_siginfo).si_signo == 0);

   if (__builtin_setjmp(VG_(scheduler_jmpbuf)) == 0) {
      /* try this ... */
      VG_(scheduler_jmpbuf_valid) = True;
      trc = VG_(run_innerloop)();
      VG_(scheduler_jmpbuf_valid) = False;
      /* We get here if the client didn't take a fault. */
   } else {
      /* We get here if the client took a fault, which caused our
         signal handler to longjmp. */
      VG_(scheduler_jmpbuf_valid) = False;
      vg_assert(trc == 0);
      trc = VG_TRC_UNRESUMABLE_SIGNAL;
   }

   vg_assert(!VG_(scheduler_jmpbuf_valid));

   VG_(save_thread_state) ( tid );
   VGP_POPCC(VgpRun);
   return trc;
}


static 
void mostly_clear_thread_record ( ThreadId tid )
{
   vg_assert(tid >= 0 && tid < VG_N_THREADS);
   VG_(threads)[tid].ldt                  = NULL;
   VG_(threads)[tid].tid                  = tid;
   VG_(threads)[tid].status               = VgTs_Empty;
   VG_(threads)[tid].associated_mx        = NULL;
   VG_(threads)[tid].associated_cv        = NULL;
   VG_(threads)[tid].awaken_at            = 0;
   VG_(threads)[tid].joinee_retval        = NULL;
   VG_(threads)[tid].joiner_thread_return = NULL;
   VG_(threads)[tid].joiner_jee_tid       = VG_INVALID_THREADID;
   VG_(threads)[tid].detached             = False;
   VG_(threads)[tid].cancel_st   = True; /* PTHREAD_CANCEL_ENABLE */
   VG_(threads)[tid].cancel_ty   = True; /* PTHREAD_CANCEL_DEFERRED */
   VG_(threads)[tid].cancel_pend = NULL; /* not pending */
   VG_(threads)[tid].custack_used = 0;
   VG_(ksigemptyset)(&VG_(threads)[tid].sig_mask);
   VG_(ksigfillset)(&VG_(threads)[tid].eff_sig_mask);
   VG_(threads)[tid].specifics_ptr = NULL;

   VG_(threads)[tid].syscallno		  = -1;
   VG_(threads)[tid].sys_pre_res	  = NULL;

   VG_(threads)[tid].proxy		  = NULL;
}



/* Initialise the scheduler.  Create a single "main" thread ready to
   run, with special ThreadId of one.  This is called at startup; the
   caller takes care to park the client's state is parked in
   VG_(baseBlock).  
*/
void VG_(scheduler_init) ( void )
{
   Int      i;
   ThreadId tid_main;

   for (i = 0 /* NB; not 1 */; i < VG_N_THREADS; i++) {
      mostly_clear_thread_record(i);
      VG_(threads)[i].stack_size           = 0;
      VG_(threads)[i].stack_base           = (Addr)NULL;
      VG_(threads)[i].stack_highest_word   = (Addr)NULL;
   }

   for (i = 0; i < VG_N_THREAD_KEYS; i++) {
      vg_thread_keys[i].inuse      = False;
      vg_thread_keys[i].destructor = NULL;
   }

   vg_fhstack_used = 0;

   /* Assert this is thread zero, which has certain magic
      properties. */
   tid_main = vg_alloc_ThreadState();
   vg_assert(tid_main == 1); 
   VG_(threads)[tid_main].status = VgTs_Runnable;

   /* Copy VG_(baseBlock) state to tid_main's slot. */
   vg_tid_currently_in_baseBlock = tid_main;
   vg_tid_last_in_baseBlock = tid_main;
   VG_(save_thread_state) ( tid_main );

   VG_(threads)[tid_main].stack_highest_word 
      = VG_(foundstack_start) + VG_(foundstack_size) - 4;
   VG_(threads)[tid_main].stack_base = VG_(foundstack_start);
   VG_(threads)[tid_main].stack_size = VG_(foundstack_size);

   /* So now ... */
   vg_assert(vg_tid_currently_in_baseBlock == VG_INVALID_THREADID);

   /* Not running client code right now. */
   VG_(scheduler_jmpbuf_valid) = False;

   /* Proxy for main thread */
   VG_(proxy_create)(tid_main);
}





/* vthread tid is returning from a signal handler; modify its
   stack/regs accordingly. */

static
void handle_signal_return ( ThreadId tid )
{
   Bool restart_blocked_syscalls;
   struct vki_timespec * rem;

   vg_assert(VG_(is_valid_tid)(tid));

   restart_blocked_syscalls = VG_(signal_returns)(tid);

   if (restart_blocked_syscalls)
      /* Easy; we don't have to do anything. */
      return;

   if (VG_(threads)[tid].status == VgTs_Sleeping
       && VG_(threads)[tid].m_eax == __NR_nanosleep) {
      /* We interrupted a nanosleep().  The right thing to do is to
         write the unused time to nanosleep's second param, but that's
         too much effort ... we just say that 1 nanosecond was not
         used, and return EINTR. */
      rem = (struct vki_timespec *)VG_(threads)[tid].m_ecx; /* arg2 */
      if (rem != NULL) {
         rem->tv_sec = 0;
         rem->tv_nsec = 1;
      }
      SET_SYSCALL_RETVAL(tid, -VKI_EINTR);
      VG_(threads)[tid].status = VgTs_Runnable;
      return;
   }

   /* All other cases?  Just return. */
}


static
void sched_do_syscall ( ThreadId tid )
{
   Int   syscall_no;
   Char  msg_buf[100];

   vg_assert(VG_(is_valid_tid)(tid));
   vg_assert(VG_(threads)[tid].status == VgTs_Runnable);

   syscall_no = VG_(threads)[tid].m_eax; /* syscall number */

   /* Special-case nanosleep because we can.  But should we?

      XXX not doing so for now, because it doesn't seem to work
      properly, and we can use the syscall nanosleep just as easily.
    */
   if (0 && syscall_no == __NR_nanosleep) {
      UInt t_now, t_awaken;
      struct vki_timespec* req;
      req = (struct vki_timespec*)VG_(threads)[tid].m_ebx; /* arg1 */

      if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000) {
	 SET_SYSCALL_RETVAL(tid, -VKI_EINVAL);
	 return;
      }

      t_now = VG_(read_millisecond_timer)();     
      t_awaken 
         = t_now
           + (UInt)1000ULL * (UInt)(req->tv_sec) 
           + (UInt)(req->tv_nsec) / 1000000;
      VG_(threads)[tid].status    = VgTs_Sleeping;
      VG_(threads)[tid].awaken_at = t_awaken;
      if (VG_(clo_trace_sched)) {
         VG_(sprintf)(msg_buf, "at %d: nanosleep for %d", 
                               t_now, t_awaken-t_now);
	 print_sched_event(tid, msg_buf);
      }
      VG_(add_timeout)(tid, t_awaken);
      /* Force the scheduler to run something else for a while. */
      return;
   }

   /* If pre_syscall returns true, then we're done immediately */
   if (VG_(pre_syscall)(tid)) {
      VG_(post_syscall(tid));
      vg_assert(VG_(threads)[tid].status == VgTs_Runnable);
   } else {
      vg_assert(VG_(threads)[tid].status == VgTs_WaitSys);
   }
}



struct timeout {
   UInt		time;		/* time we should awaken */
   ThreadId	tid;		/* thread which cares about this timeout */
   struct timeout *next;
};

static struct timeout *timeouts;

void VG_(add_timeout)(ThreadId tid, UInt time)
{
   struct timeout *t = VG_(arena_malloc)(VG_AR_CORE, sizeof(*t));
   struct timeout **prev, *tp;

   t->time = time;
   t->tid = tid;

   if (VG_(clo_trace_sched)) {
      Char msg_buf[100];
      VG_(sprintf)(msg_buf, "add_timeout: now=%u adding timeout at %u",
		   VG_(read_millisecond_timer)(), time);
      print_sched_event(tid, msg_buf);
   }

   for(tp = timeouts, prev = &timeouts; 
       tp != NULL && tp->time < time; 
       prev = &tp->next, tp = tp->next)
      ;
   t->next = tp;
   *prev = t;
}

/* Sleep for a while, but be willing to be woken. */
static
void idle ( void )
{
   struct vki_pollfd pollfd[1];
   Int delta = -1;
   Int fd = VG_(proxy_resfd)();

   pollfd[0].fd = fd;
   pollfd[0].events = VKI_POLLIN;

   /* Look though the nearest timeouts, looking for the next future
      one (there may be stale past timeouts).  They'll all be mopped
      below up when the poll() finishes. */
   if (timeouts != NULL) {
      struct timeout *tp;
      Bool wicked = False;
      UInt now = VG_(read_millisecond_timer)();

      for(tp = timeouts; tp != NULL && tp->time < now; tp = tp->next) {
	 /* If a thread is still sleeping in the past, make it runnable */
	 ThreadState *tst = VG_(get_ThreadState)(tp->tid);
	 if (tst->status == VgTs_Sleeping)
	    tst->status = VgTs_Runnable;
	 wicked = True;		/* no sleep for the wicked */
      }

      if (tp != NULL) {
	 delta = tp->time - now;
	 vg_assert(delta >= 0);
      }
      if (wicked)
	 delta = 0;
   }

   /* gotta wake up for something! */
   vg_assert(fd != -1 || delta != -1);

   /* If we need to do signal routing, then poll for pending signals
      every VG_(clo_signal_polltime) mS */
   if (VG_(do_signal_routing) && (delta > VG_(clo_signal_polltime) || delta == -1))
      delta = VG_(clo_signal_polltime);
   
   if (VG_(clo_trace_sched)) {
      Char msg_buf[100];
      VG_(sprintf)(msg_buf, "idle: waiting for %dms and fd %d",
		   delta, fd);
      print_sched_event(0, msg_buf);
   }

   VG_(poll)(pollfd, fd != -1 ? 1 : 0, delta);

   /* See if there's anything on the timeout list which needs
      waking, and mop up anything in the past. */
   {
      UInt now = VG_(read_millisecond_timer)();
      struct timeout *tp;

      tp = timeouts;

      while(tp && tp->time <= now) {
	 struct timeout *dead;
	 ThreadState *tst;
	 
	 tst = VG_(get_ThreadState)(tp->tid);
	
	 if (VG_(clo_trace_sched)) {
	    Char msg_buf[100];
	    VG_(sprintf)(msg_buf, "idle: now=%u removing timeout at %u",
			 now, tp->time);
	    print_sched_event(tp->tid, msg_buf);
	 }

	 /* If awaken_at != tp->time then it means the timeout is
	    stale and we should just ignore it. */
	 if(tst->awaken_at == tp->time) {
	    switch(tst->status) {
	    case VgTs_Sleeping:
	       tst->awaken_at = 0xFFFFFFFF;
	       tst->status = VgTs_Runnable;
	       break;

	    case VgTs_WaitCV:
	       do_pthread_cond_timedwait_TIMEOUT(tst->tid);
	       break;

	    default:
	       /* This is a bit odd but OK; if a thread had a timeout
		  but woke for some other reason (signal, condvar
		  wakeup), then it will still be on the list. */
	       if (0)
		  VG_(printf)("idle(): unexpected status tp->tid=%d tst->status = %d\n", 
			      tp->tid, tst->status);
	       break;
	    }
	 }

	 dead = tp;
	 tp = tp->next;

	 VG_(arena_free)(VG_AR_CORE, dead);
      }

      timeouts = tp;
   }
}


/* ---------------------------------------------------------------------
   The scheduler proper.
   ------------------------------------------------------------------ */

/* Run user-space threads until either
   * Deadlock occurs
   * One thread asks to shutdown Valgrind
   * The specified number of basic blocks has gone by.
*/
VgSchedReturnCode VG_(scheduler) ( void )
{
   ThreadId tid, tid_next;
   UInt     trc;
   UInt     dispatch_ctr_SAVED;
   Int      done_this_time, n_in_bounded_wait;
   Int	    n_exists, n_waiting_for_reaper;
   Addr     trans_addr;

   /* Start with the root thread.  tid in general indicates the
      currently runnable/just-finished-running thread. */
   VG_(last_run_tid) = tid = 1;

   /* This is the top level scheduler loop.  It falls into three
      phases. */
   while (True) {

      /* ======================= Phase 0 of 3 =======================
	 Be paranoid.  Always a good idea. */
     stage1:
      scheduler_sanity();
      VG_(do_sanity_checks)( False );

      /* ======================= Phase 1 of 3 =======================
         Handle I/O completions and signals.  This may change the
         status of various threads.  Then select a new thread to run,
         or declare deadlock, or sleep if there are no runnable
         threads but some are blocked on I/O.  */

      /* Was a debug-stop requested? */
      if (VG_(bbs_to_go) == 0) 
         goto debug_stop;

      /* Do the following loop until a runnable thread is found, or
         deadlock is detected. */
      while (True) {

         /* For stats purposes only. */
         VG_(num_scheduling_events_MAJOR) ++;

	 /* Route signals to their proper places */
	 VG_(route_signals)();

         /* See if any of the proxy LWPs report any activity: either a
	    syscall completing or a signal arriving. */
	 VG_(proxy_results)();

         /* Try and find a thread (tid) to run. */
         tid_next = tid;
	 if (prefer_sched != VG_INVALID_THREADID) {
	    tid_next = prefer_sched-1;
	    prefer_sched = VG_INVALID_THREADID;
	 }
         n_in_bounded_wait = 0;
	 n_exists = 0;
	 n_waiting_for_reaper = 0;
         while (True) {
            tid_next++;
            if (tid_next >= VG_N_THREADS) tid_next = 1;
            if (VG_(threads)[tid_next].status == VgTs_Sleeping
                || VG_(threads)[tid_next].status == VgTs_WaitSys
                || (VG_(threads)[tid_next].status == VgTs_WaitCV 
                    && VG_(threads)[tid_next].awaken_at != 0xFFFFFFFF))
               n_in_bounded_wait ++;
	    if (VG_(threads)[tid_next].status != VgTs_Empty)
	       n_exists++;
	    if (VG_(threads)[tid_next].status == VgTs_WaitJoiner)
	       n_waiting_for_reaper++;
            if (VG_(threads)[tid_next].status == VgTs_Runnable) 
               break; /* We can run this one. */
            if (tid_next == tid) 
               break; /* been all the way round */
         }
         tid = tid_next;
       
         if (VG_(threads)[tid].status == VgTs_Runnable) {
            /* Found a suitable candidate.  Fall out of this loop, so
               we can advance to stage 2 of the scheduler: actually
               running the thread. */
            break;
	 }

	 /* All threads have exited - pretend someone called exit() */
	 if (n_waiting_for_reaper == n_exists) {
	    VG_(exitcode) = 0;	/* ? */
	    return VgSrc_ExitSyscall;
	 }

         /* We didn't find a runnable thread.  Now what? */
         if (n_in_bounded_wait == 0) {
            /* No runnable threads and no prospect of any appearing
               even if we wait for an arbitrary length of time.  In
               short, we have a deadlock. */
	    VG_(pp_sched_status)();
            return VgSrc_Deadlock;
         }

	 /* Nothing needs doing, so sit in idle until either a timeout
	    happens or a thread's syscall completes. */
         idle();
	 /* pp_sched_status(); */
	 /* VG_(printf)("."); */
      }


      /* ======================= Phase 2 of 3 =======================
         Wahey!  We've finally decided that thread tid is runnable, so
         we now do that.  Run it for as much of a quanta as possible.
         Trivial requests are handled and the thread continues.  The
         aim is not to do too many of Phase 1 since it is expensive.  */

      if (0)
         VG_(printf)("SCHED: tid %d\n", tid);

      VG_TRACK( thread_run, tid );

      /* Figure out how many bbs to ask vg_run_innerloop to do.  Note
         that it decrements the counter before testing it for zero, so
         that if VG_(dispatch_ctr) is set to N you get at most N-1
         iterations.  Also this means that VG_(dispatch_ctr) must
         exceed zero before entering the innerloop.  Also also, the
         decrement is done before the bb is actually run, so you
         always get at least one decrement even if nothing happens.
      */
      if (VG_(bbs_to_go) >= VG_SCHEDULING_QUANTUM)
         VG_(dispatch_ctr) = VG_SCHEDULING_QUANTUM + 1;
      else
         VG_(dispatch_ctr) = (UInt)VG_(bbs_to_go) + 1;

      /* ... and remember what we asked for. */
      dispatch_ctr_SAVED = VG_(dispatch_ctr);

      /* paranoia ... */
      vg_assert(VG_(threads)[tid].tid == tid);

      /* Actually run thread tid. */
      while (True) {

         VG_(last_run_tid) = tid;

         /* For stats purposes only. */
         VG_(num_scheduling_events_MINOR) ++;

         if (0)
            VG_(message)(Vg_DebugMsg, "thread %d: running for %d bbs", 
                                      tid, VG_(dispatch_ctr) - 1 );
#        if 0
         if (VG_(bbs_done) > 31700000 + 0) {
            dispatch_ctr_SAVED = VG_(dispatch_ctr) = 2;
            VG_(translate)(&VG_(threads)[tid], VG_(threads)[tid].m_eip,
                           NULL,NULL,NULL);
         }
         vg_assert(VG_(threads)[tid].m_eip != 0);
#        endif

         trc = run_thread_for_a_while ( tid );

#        if 0
         if (0 == VG_(threads)[tid].m_eip) {
            VG_(printf)("tid = %d,  dc = %llu\n", tid, VG_(bbs_done));
            vg_assert(0 != VG_(threads)[tid].m_eip);
         }
#        endif

         /* Deal quickly with trivial scheduling events, and resume the
            thread. */

         if (trc == VG_TRC_INNER_FASTMISS) {
            vg_assert(VG_(dispatch_ctr) > 0);

            /* Trivial event.  Miss in the fast-cache.  Do a full
               lookup for it. */
            trans_addr 
               = VG_(search_transtab) ( VG_(threads)[tid].m_eip );
            if (trans_addr == (Addr)0) {
               /* Not found; we need to request a translation. */
               create_translation_for( 
                  tid, VG_(threads)[tid].m_eip ); 
               trans_addr = VG_(search_transtab) ( VG_(threads)[tid].m_eip ); 
               if (trans_addr == (Addr)0)
                  VG_(core_panic)("VG_TRC_INNER_FASTMISS: missing tt_fast entry");
            }
            continue; /* with this thread */
         }

         if (trc == VG_TRC_EBP_JMP_CLIENTREQ) {
            UInt reqno = *(UInt*)(VG_(threads)[tid].m_eax);
            /* VG_(printf)("request 0x%x\n", reqno); */

            /* Are we really absolutely totally quitting? */
            if (reqno == VG_USERREQ__LIBC_FREERES_DONE) {
               if (0 || VG_(clo_trace_syscalls) || VG_(clo_trace_sched)) {
                  VG_(message)(Vg_DebugMsg, 
                     "__libc_freeres() done; really quitting!");
               }
               return VgSrc_ExitSyscall;
            }

            do_client_request(tid);
            /* Following the request, we try and continue with the
               same thread if still runnable.  If not, go back to
               Stage 1 to select a new thread to run. */
            if (VG_(threads)[tid].status == VgTs_Runnable
                && reqno != VG_USERREQ__PTHREAD_YIELD)
               continue; /* with this thread */
            else
               goto stage1;
	 }

         if (trc == VG_TRC_EBP_JMP_SYSCALL) {
            /* Do a syscall for the vthread tid.  This could cause it
               to become non-runnable.  One special case: spot the
               client doing calls to exit() and take this as the cue
               to exit. */
#           if 0
            { UInt* esp; Int i;
              esp=(UInt*)VG_(threads)[tid].m_esp;
              VG_(printf)("\nBEFORE\n");
              for (i = 10; i >= -10; i--)
                 VG_(printf)("%2d  %p  =  0x%x\n", i, &esp[i], esp[i]);
            }
#           endif

            /* Deal with calling __libc_freeres() at exit.  When the
               client does __NR_exit, it's exiting for good.  So we
               then run VG_(__libc_freeres_wrapper).  That quits by
               doing VG_USERREQ__LIBC_FREERES_DONE, and at that point
               we really exit.  To be safe we nuke all other threads
               currently running. 

               If not valgrinding (cachegrinding, etc) don't do this.
               __libc_freeres does some invalid frees which crash
               the unprotected malloc/free system. */

            if (VG_(threads)[tid].m_eax == __NR_exit
                || VG_(threads)[tid].m_eax == __NR_exit_group
               ) {

               /* If __NR_exit, remember the supplied argument. */
               VG_(exitcode) = VG_(threads)[tid].m_ebx; /* syscall arg1 */

               /* Only run __libc_freeres if the skin says it's ok and
                  it hasn't been overridden with --run-libc-freeres=no
                  on the command line. */

               if (VG_(needs).libc_freeres && VG_(clo_run_libc_freeres)) {

                  if (VG_(clo_verbosity) > 2 
                      || VG_(clo_trace_syscalls) || VG_(clo_trace_sched)) {
                     VG_(message)(Vg_DebugMsg, 
                        "Caught __NR_exit; running __libc_freeres()");
                  }
                  VG_(nuke_all_threads_except) ( tid );
                  VG_(threads)[tid].m_eip = (UInt)(&VG_(__libc_freeres_wrapper));
                  vg_assert(VG_(threads)[tid].status == VgTs_Runnable);
                  goto stage1; /* party on, dudes (but not for much longer :) */

               } else {
                  /* We won't run __libc_freeres; just exit now. */
                  if (VG_(clo_verbosity) > 2 
                      || VG_(clo_trace_syscalls) || VG_(clo_trace_sched)) {
                     VG_(message)(Vg_DebugMsg, 
                        "Caught __NR_exit; quitting");
                  }
                  return VgSrc_ExitSyscall;
	       }

            }

            /* We've dealt with __NR_exit at this point. */
	    vg_assert(VG_(threads)[tid].m_eax != __NR_exit && 
		      VG_(threads)[tid].m_eax != __NR_exit_group);

            /* Trap syscalls to __NR_sched_yield and just have this
               thread yield instead.  Not essential, just an
               optimisation. */
	    if (VG_(threads)[tid].m_eax == __NR_sched_yield) {
               SET_SYSCALL_RETVAL(tid, 0); /* syscall returns with success */
               goto stage1; /* find a new thread to run */
	    }

            sched_do_syscall(tid);

#           if 0
            { UInt* esp; Int i;
              esp=(UInt*)VG_(threads)[tid].m_esp;
              VG_(printf)("AFTER\n");
              for (i = 10; i >= -10; i--)
                 VG_(printf)("%2d  %p  =  0x%x\n", i, &esp[i], esp[i]);
            }
#           endif

            if (VG_(threads)[tid].status == VgTs_Runnable) {
               continue; /* with this thread */
            } else {
               goto stage1;
            }
	 }

	 /* It's an event we can't quickly deal with.  Give up running
            this thread and handle things the expensive way. */
	 break;
      }

      /* ======================= Phase 3 of 3 =======================
         Handle non-trivial thread requests, mostly pthread stuff. */

      /* Ok, we've fallen out of the dispatcher for a
         non-completely-trivial reason. First, update basic-block
         counters. */

      done_this_time = (Int)dispatch_ctr_SAVED - (Int)VG_(dispatch_ctr) - 1;
      vg_assert(done_this_time >= 0);
      VG_(bbs_to_go)   -= (ULong)done_this_time;
      VG_(bbs_done)    += (ULong)done_this_time;

      if (0 && trc != VG_TRC_INNER_FASTMISS)
         VG_(message)(Vg_DebugMsg, "thread %d:   completed %d bbs, trc %d", 
                                   tid, done_this_time, (Int)trc );

      if (0 && trc != VG_TRC_INNER_FASTMISS)
         VG_(message)(Vg_DebugMsg, "thread %d:  %llu bbs, event %s", 
                                   tid, VG_(bbs_done),
                                   name_of_sched_event(trc) );

      /* Examine the thread's return code to figure out why it
         stopped. */

      switch (trc) {

         case VG_TRC_INNER_COUNTERZERO:
            /* Timeslice is out.  Let a new thread be scheduled,
               simply by doing nothing, causing us to arrive back at
               Phase 1. */
            if (VG_(bbs_to_go) == 0) {
               goto debug_stop;
            }
            vg_assert(VG_(dispatch_ctr) == 0);
            break;

         case VG_TRC_UNRESUMABLE_SIGNAL:
            /* It got a SIGSEGV/SIGBUS/SIGILL/SIGFPE, which we need to
               deliver right away.  */
	    vg_assert(VG_(unresumable_siginfo).si_signo == VKI_SIGSEGV ||
		      VG_(unresumable_siginfo).si_signo == VKI_SIGBUS  ||
		      VG_(unresumable_siginfo).si_signo == VKI_SIGILL  ||
		      VG_(unresumable_siginfo).si_signo == VKI_SIGFPE);
	    vg_assert(VG_(longjmpd_on_signal) == VG_(unresumable_siginfo).si_signo);

	    /* make sure we've unblocked the signals which the handler blocked */
	    VG_(unblock_host_signal)(VG_(longjmpd_on_signal));

	    VG_(deliver_signal)(tid, &VG_(unresumable_siginfo), False);
	    VG_(unresumable_siginfo).si_signo = 0; /* done */
	    break;

         default: 
            VG_(printf)("\ntrc = %d\n", trc);
            VG_(core_panic)("VG_(scheduler), phase 3: "
                            "unexpected thread return code");
            /* NOTREACHED */
            break;

      } /* switch (trc) */

      /* That completes Phase 3 of 3.  Return now to the top of the
	 main scheduler loop, to Phase 1 of 3. */

   } /* top-level scheduler loop */


   /* NOTREACHED */
   VG_(core_panic)("scheduler: post-main-loop ?!");
   /* NOTREACHED */

  debug_stop:
   /* If we exited because of a debug stop, print the translation 
      of the last block executed -- by translating it again, and 
      throwing away the result. */
   VG_(printf)(
      "======vvvvvvvv====== LAST TRANSLATION ======vvvvvvvv======\n");
   VG_(translate)( tid, 
                   VG_(threads)[tid].m_eip, NULL, NULL, NULL, NULL );
   VG_(printf)("\n");
   VG_(printf)(
      "======^^^^^^^^====== LAST TRANSLATION ======^^^^^^^^======\n");

   return VgSrc_BbsDone;
}

void VG_(need_resched) ( ThreadId prefer )
{
   /* Tell the scheduler now might be a good time to find a new
      runnable thread, because something happened which woke a thread
      up.

      NB: This can be called unsynchronized from either a signal
      handler, or from another LWP (ie, real kernel thread).

      In principle this could simply be a matter of setting
      VG_(dispatch_ctr) to a small value (say, 2), which would make
      any running code come back to the scheduler fairly quickly.

      However, since the scheduler implements a strict round-robin
      policy with only one priority level, there are, by definition,
      no better threads to be running than the current thread anyway,
      so we may as well ignore this hint.  For processes with a
      mixture of compute and I/O bound threads, this means the compute
      threads could introduce longish latencies before the I/O threads
      run.  For programs with only I/O bound threads, need_resched
      won't have any effect anyway.

      OK, so I've added command-line switches to enable low-latency
      syscalls and signals.  The prefer_sched variable is in effect
      the ID of a single thread which has higher priority than all the
      others.  If set, the scheduler will prefer to schedule that
      thread over all others.  Naturally, this could lead to
      starvation or other unfairness.
    */

   if (VG_(dispatch_ctr) > 10)
      VG_(dispatch_ctr) = 2;
   prefer_sched = prefer;
}


/* ---------------------------------------------------------------------
   The pthread implementation.
   ------------------------------------------------------------------ */

#include <pthread.h>
#include <errno.h>

#define VG_PTHREAD_STACK_MIN \
   (VG_PTHREAD_STACK_SIZE - VG_AR_CLIENT_STACKBASE_REDZONE_SZB)

/*  /usr/include/bits/pthreadtypes.h:
    typedef unsigned long int pthread_t;
*/


/* -----------------------------------------------------------
   Thread CREATION, JOINAGE and CANCELLATION: HELPER FNS
   -------------------------------------------------------- */

/* We've decided to action a cancellation on tid.  Make it jump to
   thread_exit_wrapper() in vg_libpthread.c, passing PTHREAD_CANCELED
   as the arg. */
static
void make_thread_jump_to_cancelhdlr ( ThreadId tid )
{
   Char msg_buf[100];
   vg_assert(VG_(is_valid_tid)(tid));

   /* Push PTHREAD_CANCELED on the stack and jump to the cancellation
      handler -- which is really thread_exit_wrapper() in
      vg_libpthread.c. */
   vg_assert(VG_(threads)[tid].cancel_pend != NULL);

   /* Push a suitable arg, and mark it as readable. */
   SET_PTHREQ_ESP(tid, VG_(threads)[tid].m_esp - 4);
   * (UInt*)(VG_(threads)[tid].m_esp) = (UInt)PTHREAD_CANCELED;
   VG_TRACK( post_mem_write, VG_(threads)[tid].m_esp, sizeof(void*) );

   /* Push a bogus return address.  It will not return, but we still
      need to have it so that the arg is at the correct stack offset.
      Don't mark as readable; any attempt to read this is and internal
      valgrind bug since thread_exit_wrapper should not return. */
   SET_PTHREQ_ESP(tid, VG_(threads)[tid].m_esp - 4);
   * (UInt*)(VG_(threads)[tid].m_esp) = 0xBEADDEEF;

   /* .cancel_pend will hold &thread_exit_wrapper */
   VG_(threads)[tid].m_eip = (UInt)VG_(threads)[tid].cancel_pend;

   VG_(proxy_abort_syscall)(tid);

   VG_(threads)[tid].status = VgTs_Runnable;

   /* Make sure we aren't cancelled again whilst handling this
      cancellation. */
   VG_(threads)[tid].cancel_st = False;
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, 
         "jump to cancellation handler (hdlr = %p)", 
         VG_(threads)[tid].cancel_pend);
      print_sched_event(tid, msg_buf);
   }
}



/* Release resources and generally clean up once a thread has finally
   disappeared. */
static
void cleanup_after_thread_exited ( ThreadId tid, Bool forcekill )
{
   vg_assert(VG_(is_valid_or_empty_tid)(tid));
   vg_assert(VG_(threads)[tid].status == VgTs_Empty);
   /* Its stack is now off-limits */
   VG_TRACK( die_mem_stack, VG_(threads)[tid].stack_base,
                            VG_(threads)[tid].stack_size );

   /* Deallocate its LDT, if it ever had one. */
   VG_(deallocate_LDT_for_thread)( VG_(threads)[tid].ldt );
   VG_(threads)[tid].ldt = NULL;

   /* Not interested in the timeout anymore */
   VG_(threads)[tid].awaken_at = 0xFFFFFFFF;

   /* Delete proxy LWP */
   VG_(proxy_delete)(tid, forcekill);
}


/* Look for matching pairs of threads waiting for joiners and threads
   waiting for joinees.  For each such pair copy the return value of
   the joinee into the joiner, let the joiner resume and discard the
   joinee. */
static
void maybe_rendezvous_joiners_and_joinees ( void )
{
   Char     msg_buf[100];
   void**   thread_return;
   ThreadId jnr, jee;

   for (jnr = 1; jnr < VG_N_THREADS; jnr++) {
      if (VG_(threads)[jnr].status != VgTs_WaitJoinee)
         continue;
      jee = VG_(threads)[jnr].joiner_jee_tid;
      if (jee == VG_INVALID_THREADID) 
         continue;
      vg_assert(VG_(is_valid_tid)(jee));
      if (VG_(threads)[jee].status != VgTs_WaitJoiner) {
	 /* if joinee has become detached, then make join fail with
	    EINVAL */
	 if (VG_(threads)[jee].detached) {
	    VG_(threads)[jnr].status = VgTs_Runnable;
	    VG_(threads)[jnr].joiner_jee_tid = VG_INVALID_THREADID;
	    SET_PTHREQ_RETVAL(jnr, VKI_EINVAL);
	 }
         continue;
      }
      /* ok!  jnr is waiting to join with jee, and jee is waiting to be
         joined by ... well, any thread.  So let's do it! */

      /* Copy return value to where joiner wants it. */
      thread_return = VG_(threads)[jnr].joiner_thread_return;
      if (thread_return != NULL) {
         /* CHECK thread_return writable */
         VG_TRACK( pre_mem_write, Vg_CorePThread, jnr,
                                  "pthread_join: thread_return",
                                  (Addr)thread_return, sizeof(void*));

         *thread_return = VG_(threads)[jee].joinee_retval;
         /* Not really right, since it makes the thread's return value
            appear to be defined even if it isn't. */
         VG_TRACK( post_mem_write, (Addr)thread_return, sizeof(void*) );
      }

      /* Joinee is discarded */
      VG_(threads)[jee].status = VgTs_Empty; /* bye! */
      cleanup_after_thread_exited ( jee, False );
      if (VG_(clo_trace_sched)) {
	 VG_(sprintf)(msg_buf,
		      "rendezvous with joinee %d.  %d resumes, %d exits.",
		      jee, jnr, jee );
         print_sched_event(jnr, msg_buf);
      }
      
      VG_TRACK( post_thread_join, jnr, jee );

      /* joiner returns with success */
      VG_(threads)[jnr].status = VgTs_Runnable;
      SET_PTHREQ_RETVAL(jnr, 0);
   }
}


/* Nuke all threads other than tid.  POSIX specifies that this should
   happen in __NR_exec, and after a __NR_fork() when I am the child,
   as POSIX requires.  Also used at process exit time with
   me==VG_INVALID_THREADID */
void VG_(nuke_all_threads_except) ( ThreadId me )
{
   ThreadId tid;
   for (tid = 1; tid < VG_N_THREADS; tid++) {
      if (tid == me
          || VG_(threads)[tid].status == VgTs_Empty)
         continue;
      if (0)
         VG_(printf)(
            "VG_(nuke_all_threads_except): nuking tid %d\n", tid);
      VG_(proxy_delete)(tid, True);
      VG_(threads)[tid].status = VgTs_Empty;
      cleanup_after_thread_exited( tid, True );
   }
}


/* -----------------------------------------------------------
   Thread CREATION, JOINAGE and CANCELLATION: REQUESTS
   -------------------------------------------------------- */

static
void do__cleanup_push ( ThreadId tid, CleanupEntry* cu )
{
   Int  sp;
   Char msg_buf[100];
   vg_assert(VG_(is_valid_tid)(tid));
   sp = VG_(threads)[tid].custack_used;
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, 
         "cleanup_push (fn %p, arg %p) -> slot %d", 
         cu->fn, cu->arg, sp);
      print_sched_event(tid, msg_buf);
   }
   vg_assert(sp >= 0 && sp <= VG_N_CLEANUPSTACK);
   if (sp == VG_N_CLEANUPSTACK)
      VG_(core_panic)("do__cleanup_push: VG_N_CLEANUPSTACK is too small."
                 "  Increase and recompile.");
   VG_(threads)[tid].custack[sp] = *cu;
   sp++;
   VG_(threads)[tid].custack_used = sp;
   SET_PTHREQ_RETVAL(tid, 0);
}


static
void do__cleanup_pop ( ThreadId tid, CleanupEntry* cu )
{
   Int  sp;
   Char msg_buf[100];
   vg_assert(VG_(is_valid_tid)(tid));
   sp = VG_(threads)[tid].custack_used;
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, "cleanup_pop from slot %d", sp-1);
      print_sched_event(tid, msg_buf);
   }
   vg_assert(sp >= 0 && sp <= VG_N_CLEANUPSTACK);
   if (sp == 0) {
     SET_PTHREQ_RETVAL(tid, -1);
     return;
   }
   sp--;
   VG_TRACK( pre_mem_write, Vg_CorePThread, tid,
                            "cleanup pop", (Addr)cu, sizeof(CleanupEntry) );
   *cu = VG_(threads)[tid].custack[sp];
   VG_TRACK( post_mem_write, (Addr)cu, sizeof(CleanupEntry) );
   VG_(threads)[tid].custack_used = sp;
   SET_PTHREQ_RETVAL(tid, 0);
}


static
void do_pthread_yield ( ThreadId tid )
{
   Char msg_buf[100];
   vg_assert(VG_(is_valid_tid)(tid));
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, "yield");
      print_sched_event(tid, msg_buf);
   }
   SET_PTHREQ_RETVAL(tid, 0);
}


static
void do__testcancel ( ThreadId tid )
{
   Char msg_buf[100];
   vg_assert(VG_(is_valid_tid)(tid));
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, "testcancel");
      print_sched_event(tid, msg_buf);
   }
   if (/* is there a cancellation pending on this thread? */
       VG_(threads)[tid].cancel_pend != NULL
       && /* is this thread accepting cancellations? */
          VG_(threads)[tid].cancel_st) {
     /* Ok, let's do the cancellation. */
     make_thread_jump_to_cancelhdlr ( tid );
   } else {
      /* No, we keep going. */
      SET_PTHREQ_RETVAL(tid, 0);
   }
}


static
void do__set_cancelstate ( ThreadId tid, Int state )
{
   Bool old_st;
   Char msg_buf[100];
   vg_assert(VG_(is_valid_tid)(tid));
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, "set_cancelstate to %d (%s)", state, 
         state==PTHREAD_CANCEL_ENABLE 
            ? "ENABLE" 
            : (state==PTHREAD_CANCEL_DISABLE ? "DISABLE" : "???"));
      print_sched_event(tid, msg_buf);
   }
   old_st = VG_(threads)[tid].cancel_st;
   if (state == PTHREAD_CANCEL_ENABLE) {
      VG_(threads)[tid].cancel_st = True;
   } else
   if (state == PTHREAD_CANCEL_DISABLE) {
      VG_(threads)[tid].cancel_st = False;
   } else {
      VG_(core_panic)("do__set_cancelstate");
   }
   SET_PTHREQ_RETVAL(tid, old_st ? PTHREAD_CANCEL_ENABLE 
                                 : PTHREAD_CANCEL_DISABLE);
}


static
void do__set_canceltype ( ThreadId tid, Int type )
{
   Bool old_ty;
   Char msg_buf[100];
   vg_assert(VG_(is_valid_tid)(tid));
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, "set_canceltype to %d (%s)", type, 
         type==PTHREAD_CANCEL_ASYNCHRONOUS 
            ? "ASYNCHRONOUS" 
            : (type==PTHREAD_CANCEL_DEFERRED ? "DEFERRED" : "???"));
      print_sched_event(tid, msg_buf);
   }
   old_ty = VG_(threads)[tid].cancel_ty;
   if (type == PTHREAD_CANCEL_ASYNCHRONOUS) {
      VG_(threads)[tid].cancel_ty = False;
   } else
   if (type == PTHREAD_CANCEL_DEFERRED) {
      VG_(threads)[tid].cancel_ty = True;
   } else {
      VG_(core_panic)("do__set_canceltype");
   }
   SET_PTHREQ_RETVAL(tid, old_ty ? PTHREAD_CANCEL_DEFERRED 
                       : PTHREAD_CANCEL_ASYNCHRONOUS);
}


/* Set or get the detach state for thread det. */
static
void do__set_or_get_detach ( ThreadId tid, 
                             Int what, ThreadId det )
{
   Char     msg_buf[100];
   /* VG_(printf)("do__set_or_get_detach tid %d what %d det %d\n", 
      tid, what, det); */
   vg_assert(VG_(is_valid_tid)(tid));
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, "set_or_get_detach %d (%s) for tid %d", what,
         what==0 ? "not-detached" : (
         what==1 ? "detached" : (
         what==2 ? "fetch old value" : "???")), 
         det );
      print_sched_event(tid, msg_buf);
   }

   if (!VG_(is_valid_tid)(det)) {
      SET_PTHREQ_RETVAL(tid, -1);
      return;
   }

   switch (what) {
      case 2: /* get */
         SET_PTHREQ_RETVAL(tid, VG_(threads)[det].detached ? 1 : 0);
         return;
      case 1:
         VG_(threads)[det].detached = True;
         SET_PTHREQ_RETVAL(tid, 0); 
	 /* wake anyone who was joining on us */
	 maybe_rendezvous_joiners_and_joinees();
         return;
      case 0: /* set not detached */
         VG_(threads)[det].detached = False;
         SET_PTHREQ_RETVAL(tid, 0);
         return;
      default:
         VG_(core_panic)("do__set_or_get_detach");
   }
}


static
void do__set_cancelpend ( ThreadId tid, 
                          ThreadId cee,
			  void (*cancelpend_hdlr)(void*) )
{
   Char msg_buf[100];

   vg_assert(VG_(is_valid_tid)(tid));
   vg_assert(VG_(threads)[tid].status == VgTs_Runnable);

   if (!VG_(is_valid_tid)(cee)) {
      if (VG_(clo_trace_sched)) {
         VG_(sprintf)(msg_buf, 
            "set_cancelpend for invalid tid %d", cee);
         print_sched_event(tid, msg_buf);
      }
      VG_(record_pthread_error)( tid, 
         "pthread_cancel: target thread does not exist, or invalid");
      SET_PTHREQ_RETVAL(tid, VKI_ESRCH);
      return;
   }

   VG_(threads)[cee].cancel_pend = cancelpend_hdlr;

   /* interrupt a pending syscall */
   VG_(proxy_abort_syscall)(cee);

   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, 
         "set_cancelpend (hdlr = %p, set by tid %d)", 
         cancelpend_hdlr, tid);
      print_sched_event(cee, msg_buf);
   }

   /* Thread doing the cancelling returns with success. */
   SET_PTHREQ_RETVAL(tid, 0);

   /* Perhaps we can nuke the cancellee right now? */
   if (!VG_(threads)[cee].cancel_ty) /* if PTHREAD_CANCEL_ASYNCHRONOUS */
      do__testcancel(cee);
}


static
void do_pthread_join ( ThreadId tid, 
                       ThreadId jee, void** thread_return )
{
   Char     msg_buf[100];
   ThreadId i;
   /* jee, the joinee, is the thread specified as an arg in thread
      tid's call to pthread_join.  So tid is the join-er. */
   vg_assert(VG_(is_valid_tid)(tid));
   vg_assert(VG_(threads)[tid].status == VgTs_Runnable);

   if (jee == tid) {
      VG_(record_pthread_error)( tid, 
         "pthread_join: attempt to join to self");
      SET_PTHREQ_RETVAL(tid, EDEADLK); /* libc constant, not a kernel one */
      vg_assert(VG_(threads)[tid].status == VgTs_Runnable);
      return;
   }

   /* Flush any completed pairs, so as to make sure what we're looking
      at is up-to-date. */
   maybe_rendezvous_joiners_and_joinees();

   /* Is this a sane request? */
   if ( ! VG_(is_valid_tid)(jee) ||
	VG_(threads)[jee].detached) {
      /* Invalid thread to join to. */
      VG_(record_pthread_error)( tid, 
         "pthread_join: target thread does not exist, invalid, or detached");
      SET_PTHREQ_RETVAL(tid, VKI_EINVAL);
      return;
   }

   /* Is anyone else already in a join-wait for jee? */
   for (i = 1; i < VG_N_THREADS; i++) {
      if (i == tid) continue;
      if (VG_(threads)[i].status == VgTs_WaitJoinee
          && VG_(threads)[i].joiner_jee_tid == jee) {
         /* Someone already did join on this thread */
         VG_(record_pthread_error)( tid, 
            "pthread_join: another thread already "
            "in join-wait for target thread");
         SET_PTHREQ_RETVAL(tid, VKI_EINVAL);
	 vg_assert(VG_(threads)[tid].status == VgTs_Runnable);
         return;
      }
   }

   /* Mark this thread as waiting for the joinee. */
   VG_(threads)[tid].status = VgTs_WaitJoinee;
   VG_(threads)[tid].joiner_thread_return = thread_return;
   VG_(threads)[tid].joiner_jee_tid = jee;

   /* Look for matching joiners and joinees and do the right thing. */
   maybe_rendezvous_joiners_and_joinees();

   /* Return value is irrelevant since this this thread becomes
      non-runnable.  maybe_resume_joiner() will cause it to return the
      right value when it resumes. */

   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, 
         "wait for joinee %d (may already be ready)", jee);
      print_sched_event(tid, msg_buf);
   }
}


/* ( void* ): calling thread waits for joiner and returns the void* to
   it.  This is one of two ways in which a thread can finally exit --
   the other is do__quit. */
static
void do__wait_joiner ( ThreadId tid, void* retval )
{
   Char msg_buf[100];
   vg_assert(VG_(is_valid_tid)(tid));
   vg_assert(VG_(threads)[tid].status == VgTs_Runnable);
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, 
         "do__wait_joiner(retval = %p) (non-detached thread exit)", retval);
      print_sched_event(tid, msg_buf);
   }
   VG_(threads)[tid].status = VgTs_WaitJoiner;
   VG_(threads)[tid].joinee_retval = retval;
   maybe_rendezvous_joiners_and_joinees();
}


/* ( no-args ): calling thread disappears from the system forever.
   Reclaim resources. */
static
void do__quit ( ThreadId tid )
{
   Char msg_buf[100];
   vg_assert(VG_(is_valid_tid)(tid));
   vg_assert(VG_(threads)[tid].status == VgTs_Runnable);
   VG_(threads)[tid].status = VgTs_Empty; /* bye! */
   cleanup_after_thread_exited ( tid, False );
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, "do__quit (detached thread exit)");
      print_sched_event(tid, msg_buf);
   }
   maybe_rendezvous_joiners_and_joinees();
   /* Return value is irrelevant; this thread will not get
      rescheduled. */
}


/* Should never be entered.  If it is, will be on the simulated
   CPU. */
static 
void do__apply_in_new_thread_bogusRA ( void )
{
   VG_(core_panic)("do__apply_in_new_thread_bogusRA");
}

/* (Fn, Arg): Create a new thread and run Fn applied to Arg in it.  Fn
   MUST NOT return -- ever.  Eventually it will do either __QUIT or
   __WAIT_JOINER.  Return the child tid to the parent. */
static
void do__apply_in_new_thread ( ThreadId parent_tid,
                               void* (*fn)(void *), 
                               void* arg )
{
   Addr     new_stack;
   UInt     new_stk_szb;
   ThreadId tid;
   Char     msg_buf[100];

   /* Paranoia ... */
   vg_assert(sizeof(pthread_t) == sizeof(UInt));

   vg_assert(VG_(threads)[parent_tid].status != VgTs_Empty);

   tid = vg_alloc_ThreadState();

   /* If we've created the main thread's tid, we're in deep trouble :) */
   vg_assert(tid != 1);
   vg_assert(VG_(is_valid_or_empty_tid)(tid));

   /* do this early, before the child gets any memory writes */
   VG_TRACK ( post_thread_create, parent_tid, tid );

   /* Create new thread with default attrs:
      deferred cancellation, not detached 
   */
   mostly_clear_thread_record(tid);
   VG_(threads)[tid].status = VgTs_Runnable;

   /* Copy the parent's CPU state into the child's, in a roundabout
      way (via baseBlock). */
   VG_(load_thread_state)(parent_tid);

   /* We inherit our parent's LDT. */
   if (VG_(threads)[parent_tid].ldt == NULL) {
      /* We hope this is the common case. */
      VG_(baseBlock)[VGOFF_(ldt)] = 0;
   } else {
      /* No luck .. we have to take a copy of the parent's. */
      VG_(threads)[tid].ldt
        = VG_(allocate_LDT_for_thread)( VG_(threads)[parent_tid].ldt );
      VG_(baseBlock)[VGOFF_(ldt)] = (UInt)VG_(threads)[tid].ldt;
   }

   VG_(save_thread_state)(tid);
   vg_tid_last_in_baseBlock = tid;

   /* Consider allocating the child a stack, if the one it already has
      is inadequate. */
   new_stk_szb = VG_PTHREAD_STACK_MIN;

   if (new_stk_szb > VG_(threads)[tid].stack_size) {
      /* Again, for good measure :) We definitely don't want to be
         allocating a stack for the main thread. */
      vg_assert(tid != 1);
      /* for now, we don't handle the case of anything other than
         assigning it for the first time. */
      vg_assert(VG_(threads)[tid].stack_size == 0);
      vg_assert(VG_(threads)[tid].stack_base == (Addr)NULL);
      new_stack = (Addr)VG_(get_memory_from_mmap)( new_stk_szb, 
                                                   "new thread stack" );
      VG_(threads)[tid].stack_base = new_stack;
      VG_(threads)[tid].stack_size = new_stk_szb;
      VG_(threads)[tid].stack_highest_word
         = new_stack + new_stk_szb 
                     - VG_AR_CLIENT_STACKBASE_REDZONE_SZB; /* -4  ??? */;
   }

   /* Having got memory to hold the thread's stack:
      - set %esp as base + size
      - mark everything below %esp inaccessible
      - mark redzone at stack end inaccessible
    */
   SET_PTHREQ_ESP(tid, VG_(threads)[tid].stack_base 
                       + VG_(threads)[tid].stack_size
                       - VG_AR_CLIENT_STACKBASE_REDZONE_SZB);

   VG_TRACK ( die_mem_stack, VG_(threads)[tid].stack_base, 
                           + new_stk_szb - VG_AR_CLIENT_STACKBASE_REDZONE_SZB);
   VG_TRACK ( ban_mem_stack, VG_(threads)[tid].m_esp, 
                             VG_AR_CLIENT_STACKBASE_REDZONE_SZB );
   
   /* push two args */
   SET_PTHREQ_ESP(tid, VG_(threads)[tid].m_esp - 8);

   VG_TRACK ( new_mem_stack, (Addr)VG_(threads)[tid].m_esp, 2 * 4 );
   VG_TRACK ( pre_mem_write, Vg_CorePThread, tid, "new thread: stack",
                             (Addr)VG_(threads)[tid].m_esp, 2 * 4 );
 
   /* push arg and (bogus) return address */
   * (UInt*)(VG_(threads)[tid].m_esp+4) = (UInt)arg;
   * (UInt*)(VG_(threads)[tid].m_esp) 
      = (UInt)&do__apply_in_new_thread_bogusRA;

   VG_TRACK ( post_mem_write, VG_(threads)[tid].m_esp, 2 * 4 );

   /* this is where we start */
   VG_(threads)[tid].m_eip = (UInt)fn;

   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, "new thread, created by %d", parent_tid );
      print_sched_event(tid, msg_buf);
   }

   /* We inherit our parent's signal mask. */
   VG_(threads)[tid].sig_mask = VG_(threads)[parent_tid].sig_mask;

   /* Now that the signal mask is set up, create a proxy LWP for this thread */
   VG_(proxy_create)(tid);

   /* Set the proxy's signal mask */
   VG_(proxy_setsigmask)(tid);

   /* return child's tid to parent */
   SET_PTHREQ_RETVAL(parent_tid, tid); /* success */
}


/* -----------------------------------------------------------
   MUTEXes
   -------------------------------------------------------- */

/* pthread_mutex_t is a struct with at 5 words:
      typedef struct
      {
        int __m_reserved;         -- Reserved for future use
        int __m_count;            -- Depth of recursive locking
        _pthread_descr __m_owner; -- Owner thread (if recursive or errcheck)
        int __m_kind;      -- Mutex kind: fast, recursive or errcheck
        struct _pthread_fastlock __m_lock;  -- Underlying fast lock
      } pthread_mutex_t;

   #define PTHREAD_MUTEX_INITIALIZER \
     {0, 0, 0, PTHREAD_MUTEX_TIMED_NP, __LOCK_INITIALIZER}
   # define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP \
     {0, 0, 0, PTHREAD_MUTEX_RECURSIVE_NP, __LOCK_INITIALIZER}
   # define PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP \
     {0, 0, 0, PTHREAD_MUTEX_ERRORCHECK_NP, __LOCK_INITIALIZER}
   # define PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP \
     {0, 0, 0, PTHREAD_MUTEX_ADAPTIVE_NP, __LOCK_INITIALIZER}

   How we use it:

   __m_kind  never changes and indicates whether or not it is recursive.

   __m_count indicates the lock count; if 0, the mutex is not owned by 
             anybody.  

   __m_owner has a ThreadId value stuffed into it.  We carefully arrange 
             that ThreadId == 0 is invalid (VG_INVALID_THREADID), so that
             statically initialised mutexes correctly appear 
             to belong to nobody.

   In summary, a not-in-use mutex is distinguised by having __m_owner
   == 0 (VG_INVALID_THREADID) and __m_count == 0 too.  If one of those
   conditions holds, the other should too.

   There is no linked list of threads waiting for this mutex.  Instead
   a thread in WaitMX state points at the mutex with its waited_on_mx
   field.  This makes _unlock() inefficient, but simple to implement the
   right semantics viz-a-viz signals.

   We don't have to deal with mutex initialisation; the client side
   deals with that for us.  
*/

/* Helper fns ... */
static
void release_one_thread_waiting_on_mutex ( pthread_mutex_t* mutex, 
                                           Char* caller )
{
   Int  i;
   Char msg_buf[100];

   /* Find some arbitrary thread waiting on this mutex, and make it
      runnable.  If none are waiting, mark the mutex as not held. */
   for (i = 1; i < VG_N_THREADS; i++) {
      if (VG_(threads)[i].status == VgTs_Empty) 
         continue;
      if (VG_(threads)[i].status == VgTs_WaitMX 
          && VG_(threads)[i].associated_mx == mutex)
         break;
   }

   VG_TRACK( post_mutex_unlock, (ThreadId)mutex->__m_owner, mutex );

   vg_assert(i <= VG_N_THREADS);
   if (i == VG_N_THREADS) {
      /* Nobody else is waiting on it. */
      mutex->__m_count = 0;
      mutex->__m_owner = VG_INVALID_THREADID;
   } else {
      /* Notionally transfer the hold to thread i, whose
         pthread_mutex_lock() call now returns with 0 (success). */
      /* The .count is already == 1. */
      vg_assert(VG_(threads)[i].associated_mx == mutex);
      mutex->__m_owner = (_pthread_descr)i;
      VG_(threads)[i].status        = VgTs_Runnable;
      VG_(threads)[i].associated_mx = NULL;
      /* m_edx already holds pth_mx_lock() success (0) */

      VG_TRACK( post_mutex_lock, (ThreadId)i, mutex);

      if (VG_(clo_trace_pthread_level) >= 1) {
         VG_(sprintf)(msg_buf, "%s       mx %p: RESUME", 
                               caller, mutex );
         print_pthread_event(i, msg_buf);
      }
   }
}


static
void do_pthread_mutex_lock( ThreadId tid, 
                            Bool is_trylock, 
                            pthread_mutex_t* mutex )
{
   Char  msg_buf[100];
   Char* caller
      = is_trylock ? "pthread_mutex_trylock"
                   : "pthread_mutex_lock   ";

   if (VG_(clo_trace_pthread_level) >= 2) {
      VG_(sprintf)(msg_buf, "%s    mx %p ...", caller, mutex );
      print_pthread_event(tid, msg_buf);
   }

   /* Paranoia ... */
   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);

   /* POSIX doesn't mandate this, but for sanity ... */
   if (mutex == NULL) {
      VG_(record_pthread_error)( tid, 
         "pthread_mutex_lock/trylock: mutex is NULL");
      SET_PTHREQ_RETVAL(tid, VKI_EINVAL);
      return;
   }

   /* More paranoia ... */
   switch (mutex->__m_kind) {
#     ifndef GLIBC_2_1
      case PTHREAD_MUTEX_TIMED_NP:
      case PTHREAD_MUTEX_ADAPTIVE_NP:
#     endif
#     ifdef GLIBC_2_1
      case PTHREAD_MUTEX_FAST_NP:
#     endif
      case PTHREAD_MUTEX_RECURSIVE_NP:
      case PTHREAD_MUTEX_ERRORCHECK_NP:
         if (mutex->__m_count >= 0) break;
         /* else fall thru */
      default:
         VG_(record_pthread_error)( tid, 
            "pthread_mutex_lock/trylock: mutex is invalid");
         SET_PTHREQ_RETVAL(tid, VKI_EINVAL);
         return;
   }

   if (mutex->__m_count > 0) {

      vg_assert(VG_(is_valid_tid)((ThreadId)mutex->__m_owner));

      /* Someone has it already. */
      if ((ThreadId)mutex->__m_owner == tid) {
         /* It's locked -- by me! */
         if (mutex->__m_kind == PTHREAD_MUTEX_RECURSIVE_NP) {
            /* return 0 (success). */
            mutex->__m_count++;
            SET_PTHREQ_RETVAL(tid, 0);
            if (0)
               VG_(printf)("!!!!!! tid %d, mx %p -> locked %d\n", 
                           tid, mutex, mutex->__m_count);
            return;
         } else {
            if (is_trylock)
               SET_PTHREQ_RETVAL(tid, EBUSY);
            else
               SET_PTHREQ_RETVAL(tid, EDEADLK);
            return;
         }
      } else {
         /* Someone else has it; we have to wait.  Mark ourselves
            thusly. */
         /* GUARD: __m_count > 0 && __m_owner is valid */
         if (is_trylock) {
            /* caller is polling; so return immediately. */
            SET_PTHREQ_RETVAL(tid, EBUSY);
         } else {
	    VG_TRACK ( pre_mutex_lock, tid, mutex );

            VG_(threads)[tid].status        = VgTs_WaitMX;
            VG_(threads)[tid].associated_mx = mutex;
            SET_PTHREQ_RETVAL(tid, 0); /* pth_mx_lock success value */
            if (VG_(clo_trace_pthread_level) >= 1) {
               VG_(sprintf)(msg_buf, "%s    mx %p: BLOCK", 
                                     caller, mutex );
               print_pthread_event(tid, msg_buf);
            }
	 }
         return;
      }

   } else {
      /* Nobody owns it.  Sanity check ... */
      vg_assert(mutex->__m_owner == VG_INVALID_THREADID);

      VG_TRACK ( pre_mutex_lock, tid, mutex );

      /* We get it! [for the first time]. */
      mutex->__m_count = 1;
      mutex->__m_owner = (_pthread_descr)tid;

      /* return 0 (success). */
      SET_PTHREQ_RETVAL(tid, 0);

      VG_TRACK( post_mutex_lock, tid, mutex);
   }
}


static
void do_pthread_mutex_unlock ( ThreadId tid,
                               pthread_mutex_t* mutex )
{
   Char msg_buf[100];

   if (VG_(clo_trace_pthread_level) >= 2) {
      VG_(sprintf)(msg_buf, "pthread_mutex_unlock     mx %p ...", mutex );
      print_pthread_event(tid, msg_buf);
   }

   /* Paranoia ... */
   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);

   if (mutex == NULL) {
      VG_(record_pthread_error)( tid, 
         "pthread_mutex_unlock: mutex is NULL");
      SET_PTHREQ_RETVAL(tid, VKI_EINVAL);
      return;
   }

   /* If this was locked before the dawn of time, pretend it was
      locked now so that it balances with unlocks */
   if (mutex->__m_kind & VG_PTHREAD_PREHISTORY) {
      mutex->__m_kind &= ~VG_PTHREAD_PREHISTORY;
      VG_TRACK( pre_mutex_lock, (ThreadId)mutex->__m_owner, mutex );
      VG_TRACK( post_mutex_lock, (ThreadId)mutex->__m_owner, mutex );
   }

   /* More paranoia ... */
   switch (mutex->__m_kind) {
#     ifndef GLIBC_2_1    
      case PTHREAD_MUTEX_TIMED_NP:
      case PTHREAD_MUTEX_ADAPTIVE_NP:
#     endif
#     ifdef GLIBC_2_1
      case PTHREAD_MUTEX_FAST_NP:
#     endif
      case PTHREAD_MUTEX_RECURSIVE_NP:
      case PTHREAD_MUTEX_ERRORCHECK_NP:
         if (mutex->__m_count >= 0) break;
         /* else fall thru */
      default:
         VG_(record_pthread_error)( tid, 
            "pthread_mutex_unlock: mutex is invalid");
         SET_PTHREQ_RETVAL(tid, VKI_EINVAL);
         return;
   }

   /* Barf if we don't currently hold the mutex. */
   if (mutex->__m_count == 0) {
      /* nobody holds it */
      VG_(record_pthread_error)( tid, 
         "pthread_mutex_unlock: mutex is not locked");
      SET_PTHREQ_RETVAL(tid, EPERM);
      return;
   }

   if ((ThreadId)mutex->__m_owner != tid) {
      /* we don't hold it */
      VG_(record_pthread_error)( tid, 
         "pthread_mutex_unlock: mutex is locked by a different thread");
      SET_PTHREQ_RETVAL(tid, EPERM);
      return;
   }

   /* If it's a multiply-locked recursive mutex, just decrement the
      lock count and return. */
   if (mutex->__m_count > 1) {
      vg_assert(mutex->__m_kind == PTHREAD_MUTEX_RECURSIVE_NP);
      mutex->__m_count --;
      SET_PTHREQ_RETVAL(tid, 0); /* success */
      return;
   }

   /* Now we're sure it is locked exactly once, and by the thread who
      is now doing an unlock on it.  */
   vg_assert(mutex->__m_count == 1);
   vg_assert((ThreadId)mutex->__m_owner == tid);

   /* Release at max one thread waiting on this mutex. */
   release_one_thread_waiting_on_mutex ( mutex, "pthread_mutex_lock" );

   /* Our (tid's) pth_unlock() returns with 0 (success). */
   SET_PTHREQ_RETVAL(tid, 0); /* Success. */
}


/* -----------------------------------------------------------
   CONDITION VARIABLES
   -------------------------------------------------------- */

/* The relevant native types are as follows:
   (copied from /usr/include/bits/pthreadtypes.h)

   -- Conditions (not abstract because of PTHREAD_COND_INITIALIZER
   typedef struct
   {
     struct _pthread_fastlock __c_lock; -- Protect against concurrent access
     _pthread_descr __c_waiting;        -- Threads waiting on this condition
   } pthread_cond_t;

   -- Attribute for conditionally variables.
   typedef struct
   {
     int __dummy;
   } pthread_condattr_t;

   #define PTHREAD_COND_INITIALIZER {__LOCK_INITIALIZER, 0}

   We don't use any fields of pthread_cond_t for anything at all.
   Only the identity of the CVs is important.

   Linux pthreads supports no attributes on condition variables, so we
   don't need to think too hard there.  */


static 
void do_pthread_cond_timedwait_TIMEOUT ( ThreadId tid )
{
   Char             msg_buf[100];
   pthread_mutex_t* mx;
   pthread_cond_t*  cv;

   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_WaitCV
             && VG_(threads)[tid].awaken_at != 0xFFFFFFFF);
   mx = VG_(threads)[tid].associated_mx;
   vg_assert(mx != NULL);
   cv = VG_(threads)[tid].associated_cv;
   vg_assert(cv != NULL);

   if (mx->__m_owner == VG_INVALID_THREADID) {
      /* Currently unheld; hand it out to thread tid. */
      vg_assert(mx->__m_count == 0);
      VG_(threads)[tid].status        = VgTs_Runnable;
      SET_PTHREQ_RETVAL(tid, ETIMEDOUT);  /* pthread_cond_wait return value */
      VG_(threads)[tid].associated_cv = NULL;
      VG_(threads)[tid].associated_mx = NULL;
      mx->__m_owner = (_pthread_descr)tid;
      mx->__m_count = 1;

      VG_TRACK( post_mutex_lock, tid, mx );

      if (VG_(clo_trace_pthread_level) >= 1) {
         VG_(sprintf)(msg_buf, 
            "pthread_cond_timedwai cv %p: TIMEOUT with mx %p", 
            cv, mx );
         print_pthread_event(tid, msg_buf);
      }
   } else {
      /* Currently held.  Make thread tid be blocked on it. */
      vg_assert(mx->__m_count > 0);
      VG_TRACK( pre_mutex_lock, tid, mx );

      VG_(threads)[tid].status        = VgTs_WaitMX;
      SET_PTHREQ_RETVAL(tid, ETIMEDOUT);  /* pthread_cond_wait return value */
      VG_(threads)[tid].associated_cv = NULL;
      VG_(threads)[tid].associated_mx = mx;
      if (VG_(clo_trace_pthread_level) >= 1) {
         VG_(sprintf)(msg_buf, 
            "pthread_cond_timedwai cv %p: TIMEOUT -> BLOCK for mx %p", 
            cv, mx );
         print_pthread_event(tid, msg_buf);
      }
   }
}


static
void release_N_threads_waiting_on_cond ( pthread_cond_t* cond, 
                                         Int n_to_release, 
                                         Char* caller )
{
   Int              i;
   Char             msg_buf[100];
   pthread_mutex_t* mx;

   while (True) {
      if (n_to_release == 0)
         return;

      /* Find a thread waiting on this CV. */
      for (i = 1; i < VG_N_THREADS; i++) {
         if (VG_(threads)[i].status == VgTs_Empty) 
            continue;
         if (VG_(threads)[i].status == VgTs_WaitCV 
             && VG_(threads)[i].associated_cv == cond)
            break;
      }
      vg_assert(i <= VG_N_THREADS);

      if (i == VG_N_THREADS) {
         /* Nobody else is waiting on it. */
         return;
      }

      mx = VG_(threads)[i].associated_mx;
      vg_assert(mx != NULL);

      VG_TRACK( pre_mutex_lock, i, mx );

      if (mx->__m_owner == VG_INVALID_THREADID) {
         /* Currently unheld; hand it out to thread i. */
         vg_assert(mx->__m_count == 0);
         VG_(threads)[i].status        = VgTs_Runnable;
         VG_(threads)[i].associated_cv = NULL;
         VG_(threads)[i].associated_mx = NULL;
         mx->__m_owner = (_pthread_descr)i;
         mx->__m_count = 1;
         /* .m_edx already holds pth_cond_wait success value (0) */

	 VG_TRACK( post_mutex_lock, i, mx );

         if (VG_(clo_trace_pthread_level) >= 1) {
            VG_(sprintf)(msg_buf, "%s   cv %p: RESUME with mx %p", 
                                  caller, cond, mx );
            print_pthread_event(i, msg_buf);
         }

      } else {
         /* Currently held.  Make thread i be blocked on it. */
         vg_assert(mx->__m_count > 0);
         VG_(threads)[i].status        = VgTs_WaitMX;
         VG_(threads)[i].associated_cv = NULL;
         VG_(threads)[i].associated_mx = mx;
         SET_PTHREQ_RETVAL(i, 0); /* pth_cond_wait success value */

         if (VG_(clo_trace_pthread_level) >= 1) {
            VG_(sprintf)(msg_buf, "%s   cv %p: BLOCK for mx %p", 
                                  caller, cond, mx );
            print_pthread_event(i, msg_buf);
         }

      }
   
      n_to_release--;
   }
}


static
void do_pthread_cond_wait ( ThreadId tid,
                            pthread_cond_t *cond, 
                            pthread_mutex_t *mutex,
			    UInt ms_end )
{
   Char msg_buf[100];

   /* If ms_end == 0xFFFFFFFF, wait forever (no timeout).  Otherwise,
      ms_end is the ending millisecond. */

   /* pre: mutex should be a valid mutex and owned by tid. */
   if (VG_(clo_trace_pthread_level) >= 2) {
      VG_(sprintf)(msg_buf, "pthread_cond_wait        cv %p, mx %p, end %d ...", 
                            cond, mutex, ms_end );
      print_pthread_event(tid, msg_buf);
   }

   /* Paranoia ... */
   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);

   if (mutex == NULL || cond == NULL) {
      VG_(record_pthread_error)( tid, 
         "pthread_cond_wait/timedwait: cond or mutex is NULL");
      SET_PTHREQ_RETVAL(tid, VKI_EINVAL);
      return;
   }

   /* More paranoia ... */
   switch (mutex->__m_kind) {
#     ifndef GLIBC_2_1    
      case PTHREAD_MUTEX_TIMED_NP:
      case PTHREAD_MUTEX_ADAPTIVE_NP:
#     endif
#     ifdef GLIBC_2_1
      case PTHREAD_MUTEX_FAST_NP:
#     endif
      case PTHREAD_MUTEX_RECURSIVE_NP:
      case PTHREAD_MUTEX_ERRORCHECK_NP:
         if (mutex->__m_count >= 0) break;
         /* else fall thru */
      default:
         VG_(record_pthread_error)( tid, 
            "pthread_cond_wait/timedwait: mutex is invalid");
         SET_PTHREQ_RETVAL(tid, VKI_EINVAL);
         return;
   }

   /* Barf if we don't currently hold the mutex. */
   if (mutex->__m_count == 0 /* nobody holds it */
       || (ThreadId)mutex->__m_owner != tid /* we don't hold it */) {
         VG_(record_pthread_error)( tid, 
            "pthread_cond_wait/timedwait: mutex is unlocked "
            "or is locked but not owned by thread");
      SET_PTHREQ_RETVAL(tid, VKI_EINVAL);
      return;
   }

   /* Queue ourselves on the condition. */
   VG_(threads)[tid].status        = VgTs_WaitCV;
   VG_(threads)[tid].associated_cv = cond;
   VG_(threads)[tid].associated_mx = mutex;
   VG_(threads)[tid].awaken_at     = ms_end;
   if (ms_end != 0xFFFFFFFF)
      VG_(add_timeout)(tid, ms_end);

   if (VG_(clo_trace_pthread_level) >= 1) {
      VG_(sprintf)(msg_buf, 
                   "pthread_cond_wait        cv %p, mx %p: BLOCK", 
                   cond, mutex );
      print_pthread_event(tid, msg_buf);
   }

   /* Release the mutex. */
   release_one_thread_waiting_on_mutex ( mutex, "pthread_cond_wait " );
}


static
void do_pthread_cond_signal_or_broadcast ( ThreadId tid, 
                                           Bool broadcast,
                                           pthread_cond_t *cond )
{
   Char  msg_buf[100];
   Char* caller 
      = broadcast ? "pthread_cond_broadcast" 
                  : "pthread_cond_signal   ";

   if (VG_(clo_trace_pthread_level) >= 2) {
      VG_(sprintf)(msg_buf, "%s   cv %p ...", 
                            caller, cond );
      print_pthread_event(tid, msg_buf);
   }

   /* Paranoia ... */
   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);

   if (cond == NULL) {
      VG_(record_pthread_error)( tid, 
         "pthread_cond_signal/broadcast: cond is NULL");
      SET_PTHREQ_RETVAL(tid, VKI_EINVAL);
      return;
   }
   
   release_N_threads_waiting_on_cond ( 
      cond,
      broadcast ? VG_N_THREADS : 1, 
      caller
   );

   SET_PTHREQ_RETVAL(tid, 0); /* success */
}


/* -----------------------------------------------------------
   THREAD SPECIFIC DATA
   -------------------------------------------------------- */

static __inline__
Bool is_valid_key ( ThreadKey k )
{
   /* k unsigned; hence no < 0 check */
   if (k >= VG_N_THREAD_KEYS) return False;
   if (!vg_thread_keys[k].inuse) return False;
   return True;
}


/* Return in %EDX a value of 1 if the key is valid, else 0. */
static
void do_pthread_key_validate ( ThreadId tid,
                               pthread_key_t key )
{
   Char msg_buf[100];

   if (VG_(clo_trace_pthread_level) >= 1) {
      VG_(sprintf)(msg_buf, "pthread_key_validate    key %p", 
                            key );
      print_pthread_event(tid, msg_buf);
   }

   vg_assert(sizeof(pthread_key_t) == sizeof(ThreadKey));
   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);

   if (is_valid_key((ThreadKey)key)) {
      SET_PTHREQ_RETVAL(tid, 1);
   } else {
      SET_PTHREQ_RETVAL(tid, 0);
   }
}


static
void do_pthread_key_create ( ThreadId tid,
                             pthread_key_t* key,
                             void (*destructor)(void*) )
{
   Int  i;
   Char msg_buf[100];

   if (VG_(clo_trace_pthread_level) >= 1) {
      VG_(sprintf)(msg_buf, "pthread_key_create      *key %p, destr %p", 
                            key, destructor );
      print_pthread_event(tid, msg_buf);
   }

   vg_assert(sizeof(pthread_key_t) == sizeof(ThreadKey));
   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);

   for (i = 0; i < VG_N_THREAD_KEYS; i++)
      if (!vg_thread_keys[i].inuse)   
         break;

   if (i == VG_N_THREAD_KEYS) {
      VG_(message)(Vg_UserMsg, "pthread_key_create() asked for too many keys (more than %d): increase VG_N_THREAD_KEYS and recompile Valgrind.",
		   VG_N_THREAD_KEYS);
      SET_PTHREQ_RETVAL(tid, EAGAIN); 
      return; 
   }

   vg_thread_keys[i].inuse      = True;
   vg_thread_keys[i].destructor = destructor;

   /* check key for addressibility */
   VG_TRACK( pre_mem_write, Vg_CorePThread, tid, "pthread_key_create: key",
                            (Addr)key, sizeof(pthread_key_t));
   *key = i;
   VG_TRACK( post_mem_write, (Addr)key, sizeof(pthread_key_t) );

   SET_PTHREQ_RETVAL(tid, 0);
}


static
void do_pthread_key_delete ( ThreadId tid, pthread_key_t key )
{
   Char msg_buf[100];
   if (VG_(clo_trace_pthread_level) >= 1) {
      VG_(sprintf)(msg_buf, "pthread_key_delete       key %d", 
                            key );
      print_pthread_event(tid, msg_buf);
   }

   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);
   
   if (!is_valid_key(key)) {
      VG_(record_pthread_error)( tid, 
         "pthread_key_delete: key is invalid");
      SET_PTHREQ_RETVAL(tid, VKI_EINVAL);
      return;
   }

   vg_thread_keys[key].inuse = False;
   vg_thread_keys[key].destructor = NULL;
   SET_PTHREQ_RETVAL(tid, 0);
}


/* Get the .specific_ptr for a thread.  Return 1 if the thread-slot
   isn't in use, so that client-space can scan all thread slots.  1
   cannot be confused with NULL or a legitimately-aligned specific_ptr
   value. */
static 
void do_pthread_getspecific_ptr ( ThreadId tid )
{
   void** specifics_ptr;
   Char   msg_buf[100];

   if (VG_(clo_trace_pthread_level) >= 2) {
      VG_(sprintf)(msg_buf, "pthread_getspecific_ptr" );
      print_pthread_event(tid, msg_buf);
   }

   vg_assert(VG_(is_valid_or_empty_tid)(tid));

   if (VG_(threads)[tid].status == VgTs_Empty) {
      SET_PTHREQ_RETVAL(tid, 1);
      return;
   }

   specifics_ptr = VG_(threads)[tid].specifics_ptr;
   vg_assert(specifics_ptr == NULL 
             || IS_ALIGNED4_ADDR(specifics_ptr));

   SET_PTHREQ_RETVAL(tid, (UInt)specifics_ptr);
}


static
void do_pthread_setspecific_ptr ( ThreadId tid, void** ptr )
{
   Char msg_buf[100];
   if (VG_(clo_trace_pthread_level) >= 1) {
      VG_(sprintf)(msg_buf, "pthread_setspecific_ptr  ptr %p", 
                            ptr );
      print_pthread_event(tid, msg_buf);
   }

   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);

   VG_(threads)[tid].specifics_ptr = ptr;
   SET_PTHREQ_RETVAL(tid, 0);
}


/* Helper for calling destructors at thread exit.  If key is valid,
   copy the thread's specific value into cu->arg and put the *key*'s
   destructor fn address in cu->fn.  Then return 0 to the caller.
   Otherwise return non-zero to the caller. */
static
void do__get_key_destr_and_spec ( ThreadId tid, 
                                  pthread_key_t key,
                                  CleanupEntry* cu )
{
   Char msg_buf[100];
   if (VG_(clo_trace_pthread_level) >= 2) {
      VG_(sprintf)(msg_buf, 
         "get_key_destr_and_arg (key = %d)", key );
      print_pthread_event(tid, msg_buf);
   }
   vg_assert(VG_(is_valid_tid)(tid));
   vg_assert(key >= 0 && key < VG_N_THREAD_KEYS);

   if (!vg_thread_keys[key].inuse) {
      SET_PTHREQ_RETVAL(tid, -1);
      return;
   }
   VG_TRACK( pre_mem_write, Vg_CorePThread, tid, "get_key_destr_and_spec: cu",
                            (Addr)cu, sizeof(CleanupEntry) );

   cu->fn = vg_thread_keys[key].destructor;
   if (VG_(threads)[tid].specifics_ptr == NULL) {
      cu->arg = NULL;
   } else {
      VG_TRACK( pre_mem_read, Vg_CorePThread, tid,
                "get_key_destr_and_spec: key",
                (Addr)(&VG_(threads)[tid].specifics_ptr[key]), 
                sizeof(void*) );
      cu->arg = VG_(threads)[tid].specifics_ptr[key];
   }

   VG_TRACK( post_mem_write, (Addr)cu, sizeof(CleanupEntry) );
   SET_PTHREQ_RETVAL(tid, 0);
}


/* ---------------------------------------------------
   SIGNALS
   ------------------------------------------------ */

/* See comment in vg_libthread.c:pthread_sigmask() regarding
   deliberate confusion of types sigset_t and vki_sigset_t.  Return 0
   for OK and 1 for some kind of addressing error, which the
   vg_libpthread.c routine turns into return values 0 and EFAULT
   respectively. */
static
void do_pthread_sigmask ( ThreadId tid,
                          Int vki_how,
                          vki_ksigset_t* newmask, 
                          vki_ksigset_t* oldmask )
{
   Char msg_buf[100];
   if (VG_(clo_trace_pthread_level) >= 1) {
      VG_(sprintf)(msg_buf, 
         "pthread_sigmask          vki_how %d, newmask %p, oldmask %p",
         vki_how, newmask, oldmask );
      print_pthread_event(tid, msg_buf);
   }

   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);

   if (newmask)
      VG_TRACK( pre_mem_read, Vg_CorePThread, tid, "pthread_sigmask: newmask",
                              (Addr)newmask, sizeof(vki_ksigset_t));
   if (oldmask)
      VG_TRACK( pre_mem_write, Vg_CorePThread, tid, "pthread_sigmask: oldmask",
                               (Addr)oldmask, sizeof(vki_ksigset_t));

   VG_(do_pthread_sigmask_SCSS_upd) ( tid, vki_how, newmask, oldmask );

   if (oldmask)
      VG_TRACK( post_mem_write, (Addr)oldmask, sizeof(vki_ksigset_t) );

   /* Success. */
   SET_PTHREQ_RETVAL(tid, 0);
}


static
void do_pthread_kill ( ThreadId tid, /* me */
                       ThreadId thread, /* thread to signal */
                       Int sig )
{
   Char msg_buf[100];

   if (VG_(clo_trace_signals) || VG_(clo_trace_pthread_level) >= 1) {
      VG_(sprintf)(msg_buf, 
         "pthread_kill            thread %d, signo %d",
         thread, sig );
      print_pthread_event(tid, msg_buf);
   }

   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);

   if (!VG_(is_valid_tid)(thread)) {
      VG_(record_pthread_error)( tid, 
         "pthread_kill: invalid target thread");
      SET_PTHREQ_RETVAL(tid, VKI_ESRCH);
      return;
   }

   if (sig == 0) {
      /* OK, signal 0 is just for testing */
      SET_PTHREQ_RETVAL(tid, 0);
      return;
   }

   if (sig < 1 || sig > VKI_KNSIG) {
      SET_PTHREQ_RETVAL(tid, VKI_EINVAL);
      return;
   }

   VG_(send_signal_to_thread)( thread, sig );
   SET_PTHREQ_RETVAL(tid, 0);
}


/* -----------------------------------------------------------
   FORK HANDLERS.
   -------------------------------------------------------- */

static 
void do__set_fhstack_used ( ThreadId tid, Int n )
{
   Char msg_buf[100];
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, "set_fhstack_used to %d", n );
      print_pthread_event(tid, msg_buf);
   }

   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);

   if (n >= 0 && n < VG_N_FORKHANDLERSTACK) {
      vg_fhstack_used = n;
      SET_PTHREQ_RETVAL(tid, 0);
   } else {
      SET_PTHREQ_RETVAL(tid, -1);
   }
}


static
void do__get_fhstack_used ( ThreadId tid )
{
   Int  n;
   Char msg_buf[100];
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, "get_fhstack_used" );
      print_pthread_event(tid, msg_buf);
   }

   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);

   n = vg_fhstack_used;
   vg_assert(n >= 0 && n < VG_N_FORKHANDLERSTACK);
   SET_PTHREQ_RETVAL(tid, n);
}

static
void do__set_fhstack_entry ( ThreadId tid, Int n, ForkHandlerEntry* fh )
{
   Char msg_buf[100];
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, "set_fhstack_entry %d to %p", n, fh );
      print_pthread_event(tid, msg_buf);
   }

   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);
   VG_TRACK( pre_mem_read, Vg_CorePThread, tid,
                           "pthread_atfork: prepare/parent/child",
                           (Addr)fh, sizeof(ForkHandlerEntry));

   if (n < 0 || n >= VG_N_FORKHANDLERSTACK) {
      SET_PTHREQ_RETVAL(tid, -1);
      return;
   } 

   vg_fhstack[n] = *fh;
   SET_PTHREQ_RETVAL(tid, 0);
}


static
void do__get_fhstack_entry ( ThreadId tid, Int n, /*OUT*/
                                                  ForkHandlerEntry* fh )
{
   Char msg_buf[100];
   if (VG_(clo_trace_sched)) {
      VG_(sprintf)(msg_buf, "get_fhstack_entry %d", n );
      print_pthread_event(tid, msg_buf);
   }

   vg_assert(VG_(is_valid_tid)(tid) 
             && VG_(threads)[tid].status == VgTs_Runnable);
   VG_TRACK( pre_mem_write, Vg_CorePThread, tid, "fork: prepare/parent/child",
                            (Addr)fh, sizeof(ForkHandlerEntry));

   if (n < 0 || n >= VG_N_FORKHANDLERSTACK) {
      SET_PTHREQ_RETVAL(tid, -1);
      return;
   } 

   *fh = vg_fhstack[n];
   SET_PTHREQ_RETVAL(tid, 0);

   VG_TRACK( post_mem_write, (Addr)fh, sizeof(ForkHandlerEntry) );
}

/* ---------------------------------------------------------------------
   Specifying shadow register values
   ------------------------------------------------------------------ */

void VG_(set_return_from_syscall_shadow) ( ThreadId tid, UInt ret_shadow )
{
   VG_(set_thread_shadow_archreg)(tid, R_EAX, ret_shadow);
}

UInt VG_(get_exit_status_shadow) ( void )
{
   return VG_(get_shadow_archreg)(R_EBX);
}


/* ---------------------------------------------------------------------
   Handle client requests.
   ------------------------------------------------------------------ */

/* Do a client request for the thread tid.  After the request, tid may
   or may not still be runnable; if not, the scheduler will have to
   choose a new thread to run.  
*/
static
void do_client_request ( ThreadId tid )
{
   UInt*        arg    = (UInt*)(VG_(threads)[tid].m_eax);
   UInt         req_no = arg[0];

   /* VG_(printf)("req no = 0x%x\n", req_no); */
   switch (req_no) {

      case VG_USERREQ__CLIENT_CALL0: {
         UInt (*f)(void) = (void*)arg[1];
         SET_CLCALL_RETVAL(tid, f ( ), (Addr)f);
         break;
      }
      case VG_USERREQ__CLIENT_CALL1: {
         UInt (*f)(UInt) = (void*)arg[1];
         SET_CLCALL_RETVAL(tid, f ( arg[2] ), (Addr)f );
         break;
      }
      case VG_USERREQ__CLIENT_CALL2: {
         UInt (*f)(UInt, UInt) = (void*)arg[1];
         SET_CLCALL_RETVAL(tid, f ( arg[2], arg[3] ), (Addr)f );
         break;
      }
      case VG_USERREQ__CLIENT_CALL3: {
         UInt (*f)(UInt, UInt, UInt) = (void*)arg[1];
         SET_CLCALL_RETVAL(tid, f ( arg[2], arg[3], arg[4] ), (Addr)f );
         break;
      }

      /* Note:  for skins that replace malloc() et al, we want to call
         the replacement versions.  For those that don't, we want to call
         VG_(cli_malloc)() et al.  We do this by calling SK_(malloc)(), which
         malloc-replacing skins must replace, but have its default definition
         call */

      /* Note: for MALLOC and FREE, must set the appropriate "lock"... see
         the comment in vg_defaults.c/SK_(malloc)() for why. */
      case VG_USERREQ__MALLOC:
         VG_(sk_malloc_called_by_scheduler) = True;
         SET_PTHREQ_RETVAL(
            tid, (UInt)SK_(malloc) ( arg[1] ) 
         );
         VG_(sk_malloc_called_by_scheduler) = False;
         break;

      case VG_USERREQ__FREE:
         VG_(sk_malloc_called_by_scheduler) = True;
         SK_(free) ( (void*)arg[1] );
         VG_(sk_malloc_called_by_scheduler) = False;
	 SET_PTHREQ_RETVAL(tid, 0); /* irrelevant */
         break;

      case VG_USERREQ__PTHREAD_GET_THREADID:
         SET_PTHREQ_RETVAL(tid, tid);
         break;

      case VG_USERREQ__RUNNING_ON_VALGRIND:
         SET_CLREQ_RETVAL(tid, 1);
         break;

      case VG_USERREQ__GET_PTHREAD_TRACE_LEVEL:
         SET_PTHREQ_RETVAL(tid, VG_(clo_trace_pthread_level));
         break;

      case VG_USERREQ__READ_MILLISECOND_TIMER:
         SET_PTHREQ_RETVAL(tid, VG_(read_millisecond_timer)());
         break;

      /* Some of these may make thread tid non-runnable, but the
         scheduler checks for that on return from this function. */
      case VG_USERREQ__PTHREAD_MUTEX_LOCK:
         do_pthread_mutex_lock( tid, False, (void *)(arg[1]) );
         break;

      case VG_USERREQ__PTHREAD_MUTEX_TRYLOCK:
         do_pthread_mutex_lock( tid, True, (void *)(arg[1]) );
         break;

      case VG_USERREQ__PTHREAD_MUTEX_UNLOCK:
         do_pthread_mutex_unlock( tid, (void *)(arg[1]) );
         break;

      case VG_USERREQ__PTHREAD_GETSPECIFIC_PTR:
 	 do_pthread_getspecific_ptr ( tid );
         break;

      case VG_USERREQ__SET_CANCELTYPE:
         do__set_canceltype ( tid, arg[1] );
         break;

      case VG_USERREQ__CLEANUP_PUSH:
         do__cleanup_push ( tid, (CleanupEntry*)(arg[1]) );
         break;

      case VG_USERREQ__CLEANUP_POP:
         do__cleanup_pop ( tid, (CleanupEntry*)(arg[1]) );
         break;

      case VG_USERREQ__TESTCANCEL:
         do__testcancel ( tid );
         break;

      case VG_USERREQ__PTHREAD_JOIN:
         do_pthread_join( tid, arg[1], (void**)(arg[2]) );
         break;

      case VG_USERREQ__PTHREAD_COND_WAIT:
         do_pthread_cond_wait( tid, 
                               (pthread_cond_t *)(arg[1]),
                               (pthread_mutex_t *)(arg[2]),
                               0xFFFFFFFF /* no timeout */ );
         break;

      case VG_USERREQ__PTHREAD_COND_TIMEDWAIT:
         do_pthread_cond_wait( tid, 
                               (pthread_cond_t *)(arg[1]),
                               (pthread_mutex_t *)(arg[2]),
                               arg[3] /* timeout millisecond point */ );
         break;

      case VG_USERREQ__PTHREAD_COND_SIGNAL:
         do_pthread_cond_signal_or_broadcast( 
            tid, 
	    False, /* signal, not broadcast */
            (pthread_cond_t *)(arg[1]) );
         break;

      case VG_USERREQ__PTHREAD_COND_BROADCAST:
         do_pthread_cond_signal_or_broadcast( 
            tid, 
	    True, /* broadcast, not signal */
            (pthread_cond_t *)(arg[1]) );
         break;

      case VG_USERREQ__PTHREAD_KEY_VALIDATE:
 	 do_pthread_key_validate ( tid, 
                                   (pthread_key_t)(arg[1]) );
	 break;

      case VG_USERREQ__PTHREAD_KEY_CREATE:
 	 do_pthread_key_create ( tid, 
                                 (pthread_key_t*)(arg[1]),
                                 (void(*)(void*))(arg[2]) );
	 break;

      case VG_USERREQ__PTHREAD_KEY_DELETE:
 	 do_pthread_key_delete ( tid, 
                                 (pthread_key_t)(arg[1]) );
 	 break;

      case VG_USERREQ__PTHREAD_SETSPECIFIC_PTR:
 	 do_pthread_setspecific_ptr ( tid, 
	                              (void**)(arg[1]) );
 	 break;

      case VG_USERREQ__PTHREAD_SIGMASK:
         do_pthread_sigmask ( tid,
                              arg[1],
                              (vki_ksigset_t*)(arg[2]),
                              (vki_ksigset_t*)(arg[3]) );
	 break;

      case VG_USERREQ__PTHREAD_KILL:
         do_pthread_kill ( tid, arg[1], arg[2] );
	 break;

      case VG_USERREQ__PTHREAD_YIELD:
         do_pthread_yield ( tid );
         /* On return from do_client_request(), the scheduler will
            select a new thread to run. */
	 break;

      case VG_USERREQ__SET_CANCELSTATE:
         do__set_cancelstate ( tid, arg[1] );
         break;

      case VG_USERREQ__SET_OR_GET_DETACH:
         do__set_or_get_detach ( tid, arg[1], arg[2] );
         break;

      case VG_USERREQ__SET_CANCELPEND:
         do__set_cancelpend ( tid, arg[1], (void(*)(void*))arg[2] );
         break;

      case VG_USERREQ__WAIT_JOINER:
         do__wait_joiner ( tid, (void*)arg[1] );
         break;

      case VG_USERREQ__QUIT:
         do__quit ( tid );
         break;

      case VG_USERREQ__APPLY_IN_NEW_THREAD:
         do__apply_in_new_thread ( tid, (void*(*)(void*))arg[1], 
                                        (void*)arg[2] );
         break;

      case VG_USERREQ__GET_KEY_D_AND_S:
         do__get_key_destr_and_spec ( tid, 
                                      (pthread_key_t)arg[1],
                                      (CleanupEntry*)arg[2] );
         break;

      case VG_USERREQ__NUKE_OTHER_THREADS:
         VG_(nuke_all_threads_except) ( tid );
         SET_PTHREQ_RETVAL(tid, 0);
         break;

      case VG_USERREQ__PTHREAD_ERROR:
         VG_(record_pthread_error)( tid, (Char*)(arg[1]) );
         SET_PTHREQ_RETVAL(tid, 0);
         break;

      case VG_USERREQ__SET_FHSTACK_USED:
         do__set_fhstack_used( tid, (Int)(arg[1]) );
         break;

      case VG_USERREQ__GET_FHSTACK_USED:
         do__get_fhstack_used( tid );
         break;

      case VG_USERREQ__SET_FHSTACK_ENTRY:
         do__set_fhstack_entry( tid, (Int)(arg[1]),
                                     (ForkHandlerEntry*)(arg[2]) );
         break;

      case VG_USERREQ__GET_FHSTACK_ENTRY:
         do__get_fhstack_entry( tid, (Int)(arg[1]),
                                     (ForkHandlerEntry*)(arg[2]) );
         break;

      case VG_USERREQ__SIGNAL_RETURNS: 
         handle_signal_return(tid);
	 break;
 
      case VG_USERREQ__PRINTF: {
         int count = 
            VG_(vmessage)( Vg_ClientMsg, (char *)arg[1], (va_list)arg[2] );
            SET_CLREQ_RETVAL( tid, count );
         break; }

      case VG_USERREQ__INTERNAL_PRINTF: {
         int count = 
            VG_(vmessage)( Vg_UserMsg, (char *)arg[1], (va_list)arg[2] );
            SET_CLREQ_RETVAL( tid, count );
         break; }

      case VG_USERREQ__PRINTF_BACKTRACE: {
         ExeContext *e = VG_(get_ExeContext)( tid );
         int count =
            VG_(vmessage)( Vg_ClientMsg, (char *)arg[1], (va_list)arg[2] );
            VG_(mini_stack_dump)(e->eips, VG_(clo_backtrace_size));
            SET_CLREQ_RETVAL( tid, count );
         break; }

      case VG_USERREQ__INTERNAL_PRINTF_BACKTRACE: {
         ExeContext *e = VG_(get_ExeContext)( tid );
         int count =
            VG_(vmessage)( Vg_UserMsg, (char *)arg[1], (va_list)arg[2] );
            VG_(mini_stack_dump)(e->eips, VG_(clo_backtrace_size));
            SET_CLREQ_RETVAL( tid, count );
         break; }

      /* Requests from the client program */

      case VG_USERREQ__DISCARD_TRANSLATIONS:
         if (VG_(clo_verbosity) > 2)
            VG_(printf)( "client request: DISCARD_TRANSLATIONS,"
                         " addr %p,  len %d\n",
                         (void*)arg[1], arg[2] );

         VG_(invalidate_translations)( arg[1], arg[2], True );

         SET_CLREQ_RETVAL( tid, 0 );     /* return value is meaningless */
	 break;

      case VG_USERREQ__COUNT_ERRORS:  
         SET_CLREQ_RETVAL( tid, VG_(n_errs_found) );
         break;

      default:
         if (VG_(needs).client_requests) {
	    UInt ret;

            if (VG_(clo_verbosity) > 2)
               VG_(printf)("client request: code %d,  addr %p,  len %d\n",
                           arg[0], (void*)arg[1], arg[2] );

	    if (SK_(handle_client_request) ( tid, arg, &ret ))
		SET_CLREQ_RETVAL(tid, ret);
         } else {
	    static Bool whined = False;

	    if (!whined) {
               // Allow for requests in core, but defined by skins, which
               // have 0 and 0 in their two high bytes.
               Char c1 = (arg[0] >> 24) & 0xff;
               Char c2 = (arg[0] >> 16) & 0xff;
               if (c1 == 0) c1 = '_';
               if (c2 == 0) c2 = '_';
	       VG_(message)(Vg_UserMsg, "Warning:\n"
                   "  unhandled client request: 0x%x (%c%c+0x%x).  Perhaps\n" 
		   "  VG_(needs).client_requests should be set?\n",
			    arg[0], c1, c2, arg[0] & 0xffff);
	       whined = True;
	    }
         }
         break;
   }
}


/* ---------------------------------------------------------------------
   Sanity checking.
   ------------------------------------------------------------------ */

/* Internal consistency checks on the sched/pthread structures. */
static
void scheduler_sanity ( void )
{
   pthread_mutex_t* mx;
   pthread_cond_t*  cv;
   Int              i;
   struct timeout*  top;
   UInt		    lasttime = 0;

   for(top = timeouts; top != NULL; top = top->next) {
      vg_assert(top->time >= lasttime);
      vg_assert(VG_(is_valid_or_empty_tid)(top->tid));

#if 0
      /* assert timeout entry is either stale, or associated with a
	 thread in the right state
	 
	 XXX disable for now - can be stale, but times happen to match
      */
      vg_assert(VG_(threads)[top->tid].awaken_at != top->time ||
		VG_(threads)[top->tid].status == VgTs_Sleeping ||
		VG_(threads)[top->tid].status == VgTs_WaitCV);
#endif

      lasttime = top->time;
   }

   /* VG_(printf)("scheduler_sanity\n"); */
   for (i = 1; i < VG_N_THREADS; i++) {
      mx = VG_(threads)[i].associated_mx;
      cv = VG_(threads)[i].associated_cv;
      if (VG_(threads)[i].status == VgTs_WaitMX) {
	 /* If we're waiting on a MX: (1) the mx is not null, (2, 3)
            it's actually held by someone, since otherwise this thread
            is deadlocked, (4) the mutex's owner is not us, since
            otherwise this thread is also deadlocked.  The logic in
            do_pthread_mutex_lock rejects attempts by a thread to lock
            a (non-recursive) mutex which it already owns.

            (2) has been seen to fail sometimes.  I don't know why.
            Possibly to do with signals. */
         vg_assert(cv == NULL);
         /* 1 */ vg_assert(mx != NULL);
	 /* 2 */ vg_assert(mx->__m_count > 0);
         /* 3 */ vg_assert(VG_(is_valid_tid)((ThreadId)mx->__m_owner));
         /* 4 */ vg_assert((UInt)i != (ThreadId)mx->__m_owner); 
      } else 
      if (VG_(threads)[i].status == VgTs_WaitCV) {
         vg_assert(cv != NULL);
         vg_assert(mx != NULL);
      } else {
         /* Unfortunately these don't hold true when a sighandler is
            running.  To be fixed. */
         /* vg_assert(cv == NULL); */
         /* vg_assert(mx == NULL); */
      }

      if (VG_(threads)[i].status != VgTs_Empty) {
         Int
         stack_used = (Addr)VG_(threads)[i].stack_highest_word 
                      - (Addr)VG_(threads)[i].m_esp;
         if (i > 1 /* not the root thread */ 
             && stack_used 
                >= (VG_PTHREAD_STACK_MIN - 1000 /* paranoia */)) {
            VG_(message)(Vg_UserMsg,
               "Error: STACK OVERFLOW: "
               "thread %d: stack used %d, available %d", 
               i, stack_used, VG_PTHREAD_STACK_MIN );
            VG_(message)(Vg_UserMsg,
               "Terminating Valgrind.  If thread(s) "
               "really need more stack, increase");
            VG_(message)(Vg_UserMsg,
               "VG_PTHREAD_STACK_SIZE in vg_include.h and recompile.");
            VG_(exit)(1);
	 }
      }
   }

   for (i = 0; i < VG_N_THREAD_KEYS; i++) {
      if (!vg_thread_keys[i].inuse)
         vg_assert(vg_thread_keys[i].destructor == NULL);
   }
}


/*--------------------------------------------------------------------*/
/*--- end                                           vg_scheduler.c ---*/
/*--------------------------------------------------------------------*/

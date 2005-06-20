
/*--------------------------------------------------------------------*/
/*--- Platform-specific syscalls stuff.      syswrap-ppc32-linux.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2005 Nicholas Nethercote <njn25@cam.ac.uk>
   Copyright (C) 2005 Cerion Armour-Brown <cerion@open-works.co.uk>

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

#include "pub_core_basics.h"
#include "pub_core_threadstate.h"
#include "pub_core_debuglog.h"
#include "pub_core_aspacemgr.h"
#include "pub_core_libcbase.h"
#include "pub_core_libcassert.h"
#include "pub_core_libcmman.h"
#include "pub_core_libcprint.h"
#include "pub_core_libcproc.h"
#include "pub_core_libcsignal.h"
#include "pub_core_main.h"          // For VG_(shutdown_actions_NORETURN)()
#include "pub_core_options.h"
#include "pub_core_scheduler.h"
#include "pub_core_sigframe.h"      // For VG_(sigframe_destroy)()
#include "pub_core_signals.h"
#include "pub_core_syscall.h"
#include "pub_core_syswrap.h"
#include "pub_core_tooliface.h"

#include "priv_types_n_macros.h"
#include "priv_syswrap-generic.h"   /* for decls of generic wrappers */
#include "priv_syswrap-linux.h"     /* for decls of linux-ish wrappers */
#include "priv_syswrap-main.h"

#include "vki_unistd.h"              /* for the __NR_* constants */


/* ---------------------------------------------------------------------
   Stacks, thread wrappers, clone
   Note.  Why is this stuff here?
   ------------------------------------------------------------------ */

/* 
   Allocate a stack for this thread.
   They're allocated lazily, but never freed.
 */
#define FILL 0xdeadbeef

// Valgrind's stack size, in words.
#define STACK_SIZE_W      16384

static UWord* allocstack(ThreadId tid)
{
   ThreadState *tst = VG_(get_ThreadState)(tid);
   UWord *sp;

   if (tst->os_state.valgrind_stack_base == 0) {
      void *stk = VG_(mmap)(0, STACK_SIZE_W * sizeof(UWord) + VKI_PAGE_SIZE,
                            VKI_PROT_READ|VKI_PROT_WRITE,
                            VKI_MAP_PRIVATE|VKI_MAP_ANONYMOUS,
                            SF_VALGRIND,
                            -1, 0);

      if (stk != (void *)-1) {
         VG_(mprotect)(stk, VKI_PAGE_SIZE, VKI_PROT_NONE); /* guard page */
         tst->os_state.valgrind_stack_base = ((Addr)stk) + VKI_PAGE_SIZE;
         tst->os_state.valgrind_stack_szB  = STACK_SIZE_W * sizeof(UWord);
      } else 
         return (UWord*)-1;
   }

   for (sp = (UWord*) tst->os_state.valgrind_stack_base;
        sp < (UWord*)(tst->os_state.valgrind_stack_base + 
                       tst->os_state.valgrind_stack_szB); 
        sp++)
      *sp = FILL;
   /* sp is left at top of stack */

   if (0)
      VG_(printf)("stack for tid %d at %p (%x); sp=%p\n",
                  tid, tst->os_state.valgrind_stack_base, 
                  *(UWord*)(tst->os_state.valgrind_stack_base), sp);

   vg_assert(VG_IS_16_ALIGNED(sp));

   return sp;
}

/* NB: this is identical the the amd64 version. */
/* Return how many bytes of this stack have not been used */
SSizeT VGA_(stack_unused)(ThreadId tid)
{
   ThreadState *tst = VG_(get_ThreadState)(tid);
   UWord* p;
   
   for (p = (UWord*)tst->os_state.valgrind_stack_base; 
        p && (p < (UWord*)(tst->os_state.valgrind_stack_base +
                           tst->os_state.valgrind_stack_szB)); 
        p++)
      if (*p != FILL)
         break;
   
   if (0)
      VG_(printf)("p=%p %x tst->os_state.valgrind_stack_base=%p\n",
                  p, *p, tst->os_state.valgrind_stack_base);
   
   return ((Addr)p) - tst->os_state.valgrind_stack_base;
}


/* Run a thread all the way to the end, then do appropriate exit actions
   (this is the last-one-out-turn-off-the-lights bit). 
*/
static void run_a_thread_NORETURN ( Word tidW )
{
   ThreadId tid = (ThreadId)tidW;

   VG_(debugLog)(1, "syscalls-ppc32-linux", 
                    "run_a_thread_NORETURN(tid=%lld): "
                       "VG_(thread_wrapper) called\n",
                       (ULong)tidW);

   /* Run the thread all the way through. */
   VgSchedReturnCode src = VG_(thread_wrapper)(tid);  

   VG_(debugLog)(1, "syscalls-ppc32-linux", 
                    "run_a_thread_NORETURN(tid=%lld): "
                       "VG_(thread_wrapper) done\n",
                       (ULong)tidW);

   Int c = VG_(count_living_threads)();
   vg_assert(c >= 1); /* stay sane */

   if (c == 1) {

      VG_(debugLog)(1, "syscalls-ppc32-linux", 
                       "run_a_thread_NORETURN(tid=%lld): "
                          "last one standing\n",
                          (ULong)tidW);

      /* We are the last one standing.  Keep hold of the lock and
         carry on to show final tool results, then exit the entire system. */
      VG_(shutdown_actions_NORETURN)(tid, src);

   } else {
      VG_(debugLog)(1, "syscalls-ppc32-linux", 
                       "run_a_thread_NORETURN(tid=%lld): "
                          "not last one standing\n",
                          (ULong)tidW);

      /* OK, thread is dead, but others still exist.  Just exit. */
      ThreadState *tst = VG_(get_ThreadState)(tid);

      /* This releases the run lock */
      VG_(exit_thread)(tid);
      vg_assert(tst->status == VgTs_Zombie);

      /* We have to use this sequence to terminate the thread to
         prevent a subtle race.  If VG_(exit_thread)() had left the
         ThreadState as Empty, then it could have been reallocated,
         reusing the stack while we're doing these last cleanups.
         Instead, VG_(exit_thread) leaves it as Zombie to prevent
         reallocation.  We need to make sure we don't touch the stack
         between marking it Empty and exiting.  Hence the
         assembler. */

     asm volatile (
        "stw %1,%0\n\t"          /* set tst->status = VgTs_Empty */
        "li  0,%2\n\t"           /* set r0 = __NR_exit */
        "lwz 3,%3\n\t"           /* set r3 = tst->os_state.exitcode */
        "sc\n\t"                 /* exit(tst->os_state.exitcode) */
        : "=m" (tst->status)
        : "n" (VgTs_Empty), "n" (__NR_exit), "m" (tst->os_state.exitcode));

      VG_(core_panic)("Thread exit failed?\n");
   }

  /*NOTREACHED*/
  vg_assert(0);
}


/* Call f(arg1), but first switch stacks, using 'stack' as the new
  stack, and use 'retaddr' as f's return-to address.  Also, clear all
   the integer registers before entering f.*/
__attribute__((noreturn))
void call_on_new_stack_0_1 ( Addr stack,
                             Addr retaddr,
                             void (*f)(Word),
                             Word arg1 );
//    r3 = stack
//    r4 = retaddr
//    r5 = f
//    r6 = arg1
asm(
"call_on_new_stack_0_1:\n"
"   mr    %r1,%r3\n\t"     // stack to %sp
"   mtlr  %r4\n\t"         // retaddr to %lr
"   mtctr %r5\n\t"         // f to count reg
"   mr %r3,%r6\n\t"        // arg1 to %r3
"   li 0,0\n\t"            // zero all GP regs
"   li 4,0\n\t"
"   li 5,0\n\t"
"   li 6,0\n\t"
"   li 7,0\n\t"
"   li 8,0\n\t"
"   li 9,0\n\t"
"   li 10,0\n\t"
"   li 11,0\n\t"
"   li 12,0\n\t"
"   li 13,0\n\t"
"   li 14,0\n\t"
"   li 15,0\n\t"
"   li 16,0\n\t"
"   li 17,0\n\t"
"   li 18,0\n\t"
"   li 19,0\n\t"
"   li 20,0\n\t"
"   li 21,0\n\t"
"   li 22,0\n\t"
"   li 23,0\n\t"
"   li 24,0\n\t"
"   li 25,0\n\t"
"   li 26,0\n\t"
"   li 27,0\n\t"
"   li 28,0\n\t"
"   li 29,0\n\t"
"   li 30,0\n\t"
"   li 31,0\n\t"
"   mtxer 0\n\t"           // CAB: Need this?
"   mtcr 0\n\t"            // CAB: Need this?
"   bctr\n\t"              // jump to dst
"   trap\n"                // should never get here
);


/*
   Allocate a stack for the main thread, and run it all the way to the
   end.  
 */
void VGP_(main_thread_wrapper_NORETURN)(ThreadId tid)
{
   VG_(debugLog)(1, "syscalls-ppc32-linux", 
                    "entering VGP_(main_thread_wrapper_NORETURN)\n");

   UWord* sp = allocstack(tid);

   /* make a stack frame */
   sp -= 16;
   *(UWord *)sp = 0;

   /* shouldn't be any other threads around yet */
   vg_assert( VG_(count_living_threads)() == 1 );

   call_on_new_stack_0_1( 
      (Addr)sp,             /* stack */
      0,                     /*bogus return address*/
      run_a_thread_NORETURN,  /* fn to call */
      (Word)tid              /* arg to give it */
   );

   /*NOTREACHED*/
   vg_assert(0);
}

#if 0
static Int start_thread_NORETURN ( void* arg )
{
   ThreadState* tst = (ThreadState*)arg;
   ThreadId     tid = tst->tid;

   run_a_thread_NORETURN ( (Word)tid );
   /*NOTREACHED*/
   vg_assert(0);
}
#endif

/* ---------------------------------------------------------------------
   clone() handling
   ------------------------------------------------------------------ */

/*
        Perform a clone system call.  clone is strange because it has
        fork()-like return-twice semantics, so it needs special
        handling here.

        Upon entry, we have:

            int (fn)(void*)     in r3
            void* child_stack   in r4
            int flags           in r5
            void* arg           in r6
            pid_t* child_tid    in r7
            pid_t* parent_tid   in r8
            void* tls_ptr       in r9

        System call requires:

            int    $__NR_clone  in r0  (sc number)
            int    flags        in r3  (sc arg1)
            void*  child_stack  in r4  (sc arg2)
            pid_t* parent_tid   in r5  (sc arg3)
            pid_t* child_tid    in r6  (sc arg4)
            void*  tls_ptr      in r7  (sc arg5)

        Returns an Int encoded in the linux-ppc32 way, not a SysRes.
 */
#define STRINGIFZ(__str) #__str
#define STRINGIFY(__str)  STRINGIFZ(__str)
#define __NR_CLONE        STRINGIFY(__NR_clone)
#define __NR_EXIT         STRINGIFY(__NR_exit)

extern
ULong do_syscall_clone_ppc32_linux ( Int (*fn)(void *), 
                                     void* stack, 
                                     Int   flags, 
                                     void* arg,
                                     Int*  child_tid, 
                                     Int*  parent_tid, 
                                     vki_modify_ldt_t * );
asm(
"\n"
"do_syscall_clone_ppc32_linux:\n"
"       stwu    1,-32(1)\n"
"       stw     29,20(1)\n"
"       stw     30,24(1)\n"
"       stw     31,28(1)\n"
"       mr      30,3\n"              // preserve fn
"       mr      31,6\n"              // preserve arg

        // setup child stack
"       rlwinm  4,4,0,~0xf\n"        // trim sp to multiple of 16 bytes
"       li      0,0\n"
"       stwu    0,-16(4)\n"          // make initial stack frame
"       mr      29,4\n"              // preserve sp

        // setup syscall
"       li      0,"__NR_CLONE"\n"    // syscall number
"       mr      3,5\n"               // syscall arg1: flags
        // r4 already setup          // syscall arg2: child_stack
"       mr      5,8\n"               // syscall arg3: parent_tid
"       mr      6,7\n"               // syscall arg4: child_tid
"       mr      7,9\n"               // syscall arg5: tls_ptr

"       sc\n"                        // clone()

"       mfcr    4\n"                 // return CR in r4 (low word of ULong)
"       cmpwi   3,0\n"               // child if retval == 0
"       bne     1f\n"                // jump if !child

        /* CHILD - call thread function */
        /* Note: 2.4 kernel doesn't set the child stack pointer,
           so we do it here.
           That does leave a small window for a signal to be delivered
           on the wrong stack, unfortunately. */
"       mr      1,29\n"
"       mtctr   30\n"                // ctr reg = fn
"       mr      3,31\n"              // r3 = arg
"       bctrl\n"                     // call fn()

        // exit with result
"       li      0,"__NR_EXIT"\n"
"       sc\n"

        // Exit returned?!
"       .long   0\n"

        // PARENT or ERROR - return
"1:     lwz     29,20(1)\n"
"       lwz     30,24(1)\n"
"       lwz     31,28(1)\n"
"       addi    1,1,32\n"
"       blr\n"
);

#undef __NR_CLONE
#undef __NR_EXIT
#undef STRINGIFY
#undef STRINGIFZ

// forward declarations
//.. static void setup_child ( ThreadArchState*, ThreadArchState*, Bool );
//.. static SysRes sys_set_thread_area ( ThreadId, vki_modify_ldt_t* );

/* 
   When a client clones, we need to keep track of the new thread.  This means:
   1. allocate a ThreadId+ThreadState+stack for the the thread

   2. initialize the thread's new VCPU state

   3. create the thread using the same args as the client requested,
   but using the scheduler entrypoint for IP, and a separate stack
   for SP.
 */
//.. static SysRes do_clone ( ThreadId ptid, 
//..                          UInt flags, Addr esp, 
//..                          Int *parent_tidptr, 
//..                          Int *child_tidptr, 
//..                          vki_modify_ldt_t *tlsinfo)
//.. {
//..    static const Bool debug = False;
//.. 
//..    ThreadId ctid = VG_(alloc_ThreadState)();
//..    ThreadState *ptst = VG_(get_ThreadState)(ptid);
//..    ThreadState *ctst = VG_(get_ThreadState)(ctid);
//..    UWord *stack;
//..    Segment *seg;
//..    Int ret;
//..    vki_sigset_t blockall, savedmask;
//.. 
//..    VG_(sigfillset)(&blockall);
//.. 
//..    vg_assert(VG_(is_running_thread)(ptid));
//..    vg_assert(VG_(is_valid_tid)(ctid));
//.. 
//..    stack = allocstack(ctid);

//?   /* make a stack frame */
//?   stack -= 16;
//?   *(UWord *)stack = 0;

//.. 
//..    /* Copy register state
//.. 
//..       Both parent and child return to the same place, and the code
//..       following the clone syscall works out which is which, so we
//..       don't need to worry about it.
//.. 
//..       The parent gets the child's new tid returned from clone, but the
//..       child gets 0.
//.. 
//..       If the clone call specifies a NULL esp for the new thread, then
//..       it actually gets a copy of the parent's esp.
//..    */
//..    VGA_(setup_child)( &ctst->arch, &ptst->arch );
//.. 
//..   /* Make sys_clone appear to have returned Success(0) in the
//..      child. */
//..   ctst->arch.vex.guest_GPR3 = 0;
//..   
//.. //guest_CC_OP = 1
//.. //guest_CC_DEP1 = 0x20000000
//.. 
//..    if (esp != 0)
//..       ctst->arch.vex.guest_ESP = esp;
//.. 
//..    ctst->os_state.parent = ptid;
//..    ctst->os_state.clone_flags = flags;
//..    ctst->os_state.parent_tidptr = parent_tidptr;
//..    ctst->os_state.child_tidptr = child_tidptr;
//.. 
//..    /* inherit signal mask */
//..    ctst->sig_mask = ptst->sig_mask;
//..    ctst->tmp_sig_mask = ptst->sig_mask;
//.. 
//..    /* We don't really know where the client stack is, because its
//..       allocated by the client.  The best we can do is look at the
//..       memory mappings and try to derive some useful information.  We
//..       assume that esp starts near its highest possible value, and can
//..       only go down to the start of the mmaped segment. */
//..    seg = VG_(find_segment)((Addr)esp);
//..    if (seg) {
//..       ctst->client_stack_highest_word = (Addr)PGROUNDUP(esp);
//..       ctst->client_stack_szB  = ctst->client_stack_highest_word - seg->addr;
//.. 
//..       if (debug)
//.. 	 VG_(printf)("tid %d: guessed client stack range %p-%p\n",
//.. 		     ctid, seg->addr, PGROUNDUP(esp));
//..    } else {
//..       VG_(message)(Vg_UserMsg, "!? New thread %d starts with ESP(%p) unmapped\n",
//.. 		   ctid, esp);
//..       ctst->client_stack_szB  = 0;
//..    }
//.. 
//..    if (flags & VKI_CLONE_SETTLS) {
//..       if (debug)
//.. 	 VG_(printf)("clone child has SETTLS: tls info at %p: idx=%d base=%p limit=%x; esp=%p fs=%x gs=%x\n",
//.. 		     tlsinfo, tlsinfo->entry_number, tlsinfo->base_addr, tlsinfo->limit,
//.. 		     ptst->arch.vex.guest_ESP,
//.. 		     ctst->arch.vex.guest_FS, ctst->arch.vex.guest_GS);
//..       ret = VG_(sys_set_thread_area)(ctid, tlsinfo);
//.. 
//..       if (ret != 0)
//.. 	 goto out;
//..    }
//.. 
//..    flags &= ~VKI_CLONE_SETTLS;
//.. 
//..    /* start the thread with everything blocked */
//..    VG_(sigprocmask)(VKI_SIG_SETMASK, &blockall, &savedmask);
//.. 
//..    /* Create the new thread */
//..    ret = VG_(clone)(start_thread, stack, flags, &VG_(threads)[ctid],
//.. 		    child_tidptr, parent_tidptr, NULL);
//.. 
//..    VG_(sigprocmask)(VKI_SIG_SETMASK, &savedmask, NULL);
//.. 
//..   out:
//..    if (ret < 0) {
//..       /* clone failed */
//..       VGA_(cleanup_thread)(&ctst->arch);
//..       ctst->status = VgTs_Empty;
//..    }
//.. 
//..    return ret;
//.. }


/* Do a clone which is really a fork() */
//.. static Int do_fork_clone( ThreadId tid,
//.. 			  UInt flags, Addr esp,
//.. 			  Int* parent_tidptr,
//.. 			  Int* child_tidptr )
//.. {
//..    vki_sigset_t fork_saved_mask;
//..    vki_sigset_t mask;
//..    Int ret;
//.. 
//..    if (flags & (VKI_CLONE_SETTLS | VKI_CLONE_FS | VKI_CLONE_VM | VKI_CLONE_FILES | VKI_CLONE_VFORK))
//..       return -VKI_EINVAL;
//.. 
//..    /* Block all signals during fork, so that we can fix things up in
//..       the child without being interrupted. */
//..    VG_(sigfillset)(&mask);
//..    VG_(sigprocmask)(VKI_SIG_SETMASK, &mask, &fork_saved_mask);
//.. 
//..    VG_(do_atfork_pre)(tid);
//.. 
//..    /* Since this is the fork() form of clone, we don't need all that
//..       VG_(clone) stuff */
//..    ret = VG_(do_syscall5)(__NR_clone, flags, (UWord)NULL, (UWord)parent_tidptr, 
//..                                              (UWord)NULL, (UWord)child_tidptr);
//.. 
//..    if (ret == 0) {
//..       /* child */
//..       VG_(do_atfork_child)(tid);
//.. 
//..       /* restore signal mask */
//..       VG_(sigprocmask)(VKI_SIG_SETMASK, &fork_saved_mask, NULL);
//..    } else if (ret > 0) {
//..       /* parent */
//..       if (VG_(clo_trace_syscalls))
//.. 	  VG_(printf)("   clone(fork): process %d created child %d\n", VG_(getpid)(), ret);
//.. 
//..       VG_(do_atfork_parent)(tid);
//.. 
//..       /* restore signal mask */
//..       VG_(sigprocmask)(VKI_SIG_SETMASK, &fork_saved_mask, NULL);
//..    }
//.. 
//..    return ret;
//.. }




/* ---------------------------------------------------------------------
   LDT/GDT simulation
   ------------------------------------------------------------------ */

/* Details of the LDT simulation
   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  
   When a program runs natively, the linux kernel allows each *thread*
   in it to have its own LDT.  Almost all programs never do this --
   it's wildly unportable, after all -- and so the kernel never
   allocates the structure, which is just as well as an LDT occupies
   64k of memory (8192 entries of size 8 bytes).

   A thread may choose to modify its LDT entries, by doing the
   __NR_modify_ldt syscall.  In such a situation the kernel will then
   allocate an LDT structure for it.  Each LDT entry is basically a
   (base, limit) pair.  A virtual address in a specific segment is
   translated to a linear address by adding the segment's base value.
   In addition, the virtual address must not exceed the limit value.

   To use an LDT entry, a thread loads one of the segment registers
   (%cs, %ss, %ds, %es, %fs, %gs) with the index of the LDT entry (0
   .. 8191) it wants to use.  In fact, the required value is (index <<
   3) + 7, but that's not important right now.  Any normal instruction
   which includes an addressing mode can then be made relative to that
   LDT entry by prefixing the insn with a so-called segment-override
   prefix, a byte which indicates which of the 6 segment registers
   holds the LDT index.

   Now, a key constraint is that valgrind's address checks operate in
   terms of linear addresses.  So we have to explicitly translate
   virtual addrs into linear addrs, and that means doing a complete
   LDT simulation.

   Calls to modify_ldt are intercepted.  For each thread, we maintain
   an LDT (with the same normally-never-allocated optimisation that
   the kernel does).  This is updated as expected via calls to
   modify_ldt.

   When a thread does an amode calculation involving a segment
   override prefix, the relevant LDT entry for the thread is
   consulted.  It all works.

   There is a conceptual problem, which appears when switching back to
   native execution, either temporarily to pass syscalls to the
   kernel, or permanently, when debugging V.  Problem at such points
   is that it's pretty pointless to copy the simulated machine's
   segment registers to the real machine, because we'd also need to
   copy the simulated LDT into the real one, and that's prohibitively
   expensive.

   Fortunately it looks like no syscalls rely on the segment regs or
   LDT being correct, so we can get away with it.  Apart from that the
   simulation is pretty straightforward.  All 6 segment registers are
   tracked, although only %ds, %es, %fs and %gs are allowed as
   prefixes.  Perhaps it could be restricted even more than that -- I
   am not sure what is and isn't allowed in user-mode.
*/

//.. /* Translate a struct modify_ldt_ldt_s to a VexGuestX86SegDescr, using
//..    the Linux kernel's logic (cut-n-paste of code in
//..    linux/kernel/ldt.c).  */
//.. 
//.. static
//.. void translate_to_hw_format ( /* IN  */ vki_modify_ldt_t* inn,
//.. 			      /* OUT */ VexGuestX86SegDescr* out,
//..                                         Int oldmode )
//.. {
//..    UInt entry_1, entry_2;
//..    vg_assert(8 == sizeof(VexGuestX86SegDescr));
//.. 
//..    if (0)
//..       VG_(printf)("translate_to_hw_format: base %p, limit %d\n", 
//..                   inn->base_addr, inn->limit );
//.. 
//..    /* Allow LDTs to be cleared by the user. */
//..    if (inn->base_addr == 0 && inn->limit == 0) {
//..       if (oldmode ||
//..           (inn->contents == 0      &&
//..            inn->read_exec_only == 1   &&
//..            inn->seg_32bit == 0      &&
//..            inn->limit_in_pages == 0   &&
//..            inn->seg_not_present == 1   &&
//..            inn->useable == 0 )) {
//..          entry_1 = 0;
//..          entry_2 = 0;
//..          goto install;
//..       }
//..    }
//.. 
//..    entry_1 = ((inn->base_addr & 0x0000ffff) << 16) |
//..              (inn->limit & 0x0ffff);
//..    entry_2 = (inn->base_addr & 0xff000000) |
//..              ((inn->base_addr & 0x00ff0000) >> 16) |
//..              (inn->limit & 0xf0000) |
//..              ((inn->read_exec_only ^ 1) << 9) |
//..              (inn->contents << 10) |
//..              ((inn->seg_not_present ^ 1) << 15) |
//..              (inn->seg_32bit << 22) |
//..              (inn->limit_in_pages << 23) |
//..              0x7000;
//..    if (!oldmode)
//..       entry_2 |= (inn->useable << 20);
//.. 
//..    /* Install the new entry ...  */
//..   install:
//..    out->LdtEnt.Words.word1 = entry_1;
//..    out->LdtEnt.Words.word2 = entry_2;
//.. }
//.. 
//.. 
//.. /*
//..  * linux/kernel/ldt.c
//..  *
//..  * Copyright (C) 1992 Krishna Balasubramanian and Linus Torvalds
//..  * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
//..  */
//.. 
//.. /*
//..  * read_ldt() is not really atomic - this is not a problem since
//..  * synchronization of reads and writes done to the LDT has to be
//..  * assured by user-space anyway. Writes are atomic, to protect
//..  * the security checks done on new descriptors.
//..  */
//.. static
//.. Int read_ldt ( ThreadId tid, UChar* ptr, UInt bytecount )
//.. {
//..    Int    err;
//..    UInt   i, size;
//..    UChar* ldt;
//.. 
//..    if (0)
//..       VG_(printf)("read_ldt: tid = %d, ptr = %p, bytecount = %d\n",
//..                   tid, ptr, bytecount );
//.. 
//..    vg_assert(sizeof(HWord) == sizeof(VexGuestX86SegDescr*));
//..    vg_assert(8 == sizeof(VexGuestX86SegDescr));
//.. 
//..    ldt = (Char*)(VG_(threads)[tid].arch.vex.guest_LDT);
//..    err = 0;
//..    if (ldt == NULL)
//..       /* LDT not allocated, meaning all entries are null */
//..       goto out;
//.. 
//..    size = VEX_GUEST_X86_LDT_NENT * sizeof(VexGuestX86SegDescr);
//..    if (size > bytecount)
//..       size = bytecount;
//.. 
//..    err = size;
//..    for (i = 0; i < size; i++)
//..       ptr[i] = ldt[i];
//.. 
//..   out:
//..    return err;
//.. }
//.. 
//.. 
//.. static
//.. Int write_ldt ( ThreadId tid, void* ptr, UInt bytecount, Int oldmode )
//.. {
//..    Int error;
//..    VexGuestX86SegDescr* ldt;
//..    vki_modify_ldt_t* ldt_info; 
//.. 
//..    if (0)
//..       VG_(printf)("write_ldt: tid = %d, ptr = %p, "
//..                   "bytecount = %d, oldmode = %d\n",
//..                   tid, ptr, bytecount, oldmode );
//.. 
//..    vg_assert(8 == sizeof(VexGuestX86SegDescr));
//..    vg_assert(sizeof(HWord) == sizeof(VexGuestX86SegDescr*));
//.. 
//..    ldt      = (VexGuestX86SegDescr*)VG_(threads)[tid].arch.vex.guest_LDT;
//..    ldt_info = (vki_modify_ldt_t*)ptr;
//.. 
//..    error = -VKI_EINVAL;
//..    if (bytecount != sizeof(vki_modify_ldt_t))
//..       goto out;
//.. 
//..    error = -VKI_EINVAL;
//..    if (ldt_info->entry_number >= VEX_GUEST_X86_LDT_NENT)
//..       goto out;
//..    if (ldt_info->contents == 3) {
//..       if (oldmode)
//..          goto out;
//..       if (ldt_info->seg_not_present == 0)
//..          goto out;
//..    }
//.. 
//..    /* If this thread doesn't have an LDT, we'd better allocate it
//..       now. */
//..    if (ldt == (HWord)NULL) {
//..       ldt = VG_(alloc_zeroed_x86_LDT)();
//..       VG_(threads)[tid].arch.vex.guest_LDT = (HWord)ldt;
//..    }
//.. 
//..    /* Install the new entry ...  */
//..    translate_to_hw_format ( ldt_info, &ldt[ldt_info->entry_number], oldmode );
//..    error = 0;
//.. 
//..   out:
//..    return error;
//.. }
//.. 
//.. 
//.. Int VG_(sys_modify_ldt) ( ThreadId tid,
//..                           Int func, void* ptr, UInt bytecount )
//.. {
//..    Int ret = -VKI_ENOSYS;
//.. 
//..    switch (func) {
//..    case 0:
//..       ret = read_ldt(tid, ptr, bytecount);
//..       break;
//..    case 1:
//..       ret = write_ldt(tid, ptr, bytecount, 1);
//..       break;
//..    case 2:
//..       VG_(unimplemented)("sys_modify_ldt: func == 2");
//..       /* god knows what this is about */
//..       /* ret = read_default_ldt(ptr, bytecount); */
//..       /*UNREACHED*/
//..       break;
//..    case 0x11:
//..       ret = write_ldt(tid, ptr, bytecount, 0);
//..       break;
//..    }
//..    return ret;
//.. }
//.. 
//.. 
//.. Int VG_(sys_set_thread_area) ( ThreadId tid,
//..                                vki_modify_ldt_t* info )
//.. {
//..    Int idx;
//..    VexGuestX86SegDescr* gdt;
//.. 
//..    vg_assert(8 == sizeof(VexGuestX86SegDescr));
//..    vg_assert(sizeof(HWord) == sizeof(VexGuestX86SegDescr*));
//.. 
//..    if (info == NULL)
//..       return -VKI_EFAULT;
//.. 
//..    gdt = (VexGuestX86SegDescr*)VG_(threads)[tid].arch.vex.guest_GDT;
//.. 
//..    /* If the thread doesn't have a GDT, allocate it now. */
//..    if (!gdt) {
//..       gdt = VG_(alloc_zeroed_x86_GDT)();
//..       VG_(threads)[tid].arch.vex.guest_GDT = (HWord)gdt;
//..    }
//.. 
//..    idx = info->entry_number;
//.. 
//..    if (idx == -1) {
//..       /* Find and use the first free entry. */
//..       for (idx = 0; idx < VEX_GUEST_X86_GDT_NENT; idx++) {
//..          if (gdt[idx].LdtEnt.Words.word1 == 0 
//..              && gdt[idx].LdtEnt.Words.word2 == 0)
//..             break;
//..       }
//.. 
//..       if (idx == VEX_GUEST_X86_GDT_NENT)
//..          return -VKI_ESRCH;
//..    } else if (idx < 0 || idx >= VEX_GUEST_X86_GDT_NENT) {
//..       return -VKI_EINVAL;
//..    }
//.. 
//..    translate_to_hw_format(info, &gdt[idx], 0);
//.. 
//..    VG_TRACK( pre_mem_write, Vg_CoreSysCall, tid,
//..              "set_thread_area(info->entry)",
//..              (Addr) & info->entry_number, sizeof(unsigned int) );
//..    info->entry_number = idx;
//..    VG_TRACK( post_mem_write, Vg_CoreSysCall, tid,
//..              (Addr) & info->entry_number, sizeof(unsigned int) );
//.. 
//..    return 0;
//.. }
//.. 
//.. 
//.. Int VG_(sys_get_thread_area) ( ThreadId tid,
//..                                vki_modify_ldt_t* info )
//.. {
//..    Int idx;
//..    VexGuestX86SegDescr* gdt;
//.. 
//..    vg_assert(sizeof(HWord) == sizeof(VexGuestX86SegDescr*));
//..    vg_assert(8 == sizeof(VexGuestX86SegDescr));
//.. 
//..    if (info == NULL)
//..       return -VKI_EFAULT;
//.. 
//..    idx = info->entry_number;
//.. 
//..    if (idx < 0 || idx >= VEX_GUEST_X86_GDT_NENT)
//..       return -VKI_EINVAL;
//.. 
//..    gdt = (VexGuestX86SegDescr*)VG_(threads)[tid].arch.vex.guest_GDT;
//.. 
//..    /* If the thread doesn't have a GDT, allocate it now. */
//..    if (!gdt) {
//..       gdt = VG_(alloc_zeroed_x86_GDT)();
//..       VG_(threads)[tid].arch.vex.guest_GDT = (HWord)gdt;
//..    }
//.. 
//..    info->base_addr = ( gdt[idx].LdtEnt.Bits.BaseHi << 24 ) |
//..                      ( gdt[idx].LdtEnt.Bits.BaseMid << 16 ) |
//..                      gdt[idx].LdtEnt.Bits.BaseLow;
//..    info->limit = ( gdt[idx].LdtEnt.Bits.LimitHi << 16 ) |
//..                    gdt[idx].LdtEnt.Bits.LimitLow;
//..    info->seg_32bit = gdt[idx].LdtEnt.Bits.Default_Big;
//..    info->contents = ( gdt[idx].LdtEnt.Bits.Type >> 2 ) & 0x3;
//..    info->read_exec_only = ( gdt[idx].LdtEnt.Bits.Type & 0x1 ) ^ 0x1;
//..    info->limit_in_pages = gdt[idx].LdtEnt.Bits.Granularity;
//..    info->seg_not_present = gdt[idx].LdtEnt.Bits.Pres ^ 0x1;
//..    info->useable = gdt[idx].LdtEnt.Bits.Sys;
//..    info->reserved = 0;
//.. 
//..    return 0;
//.. }



/* ---------------------------------------------------------------------
   More thread stuff
   ------------------------------------------------------------------ */

void VGP_(cleanup_thread) ( ThreadArchState* arch )
{
//..    /* Release arch-specific resources held by this thread. */
//..    /* On x86, we have to dump the LDT and GDT. */
//..    deallocate_LGDTs_for_thread( &arch->vex );
}  


//.. void VGA_(setup_child) ( /*OUT*/ ThreadArchState *child,
//..                          /*IN*/  ThreadArchState *parent )
//.. {
//..    /* We inherit our parent's guest state. */
//..    child->vex = parent->vex;
//..    child->vex_shadow = parent->vex_shadow;
//..    /* We inherit our parent's LDT. */
//..    if (parent->vex.guest_LDT == (HWord)NULL) {
//..       /* We hope this is the common case. */
//..       child->vex.guest_LDT = (HWord)NULL;
//..    } else {
//..       /* No luck .. we have to take a copy of the parent's. */
//..       child->vex.guest_LDT = (HWord)VG_(alloc_zeroed_x86_LDT)();
//..       copy_LDT_from_to( (VexGuestX86SegDescr*)parent->vex.guest_LDT,
//..                         (VexGuestX86SegDescr*)child->vex.guest_LDT );
//..    }
//..
//..    /* We need an empty GDT. */
//..    child->vex.guest_GDT = (HWord)NULL;
//.. }



/* ---------------------------------------------------------------------
   PRE/POST wrappers for ppc32/Linux-specific syscalls
   ------------------------------------------------------------------ */

#define PRE(name)       DEFN_PRE_TEMPLATE(ppc32_linux, name)
#define POST(name)      DEFN_POST_TEMPLATE(ppc32_linux, name)

/* Add prototypes for the wrappers declared here, so that gcc doesn't
   harass us for not having prototypes.  Really this is a kludge --
   the right thing to do is to make these wrappers 'static' since they
   aren't visible outside this file, but that requires even more macro
   magic. */

DECL_TEMPLATE(ppc32_linux, sys_stat64);
DECL_TEMPLATE(ppc32_linux, sys_fstat64);

// XXX: lstat64/fstat64/stat64 are generic, but not necessarily
// applicable to every architecture -- I think only to 32-bit archs.
// We're going to need something like linux/core_os32.h for such
// things, eventually, I think.  --njn
PRE(sys_stat64)
{
   PRINT("sys_stat64 ( %p, %p )",ARG1,ARG2);
   PRE_REG_READ2(long, "stat64", char *, file_name, struct stat64 *, buf);
   PRE_MEM_RASCIIZ( "stat64(file_name)", ARG1 );
   PRE_MEM_WRITE( "stat64(buf)", ARG2, sizeof(struct vki_stat64) );
}

POST(sys_stat64)
{
   POST_MEM_WRITE( ARG2, sizeof(struct vki_stat64) );
}

PRE(sys_fstat64)
{
  PRINT("sys_fstat64 ( %d, %p )",ARG1,ARG2);
  PRE_REG_READ2(long, "fstat64", unsigned long, fd, struct stat64 *, buf);
  PRE_MEM_WRITE( "fstat64(buf)", ARG2, sizeof(struct vki_stat64) );
}
POST(sys_fstat64)
{
  POST_MEM_WRITE( ARG2, sizeof(struct vki_stat64) );
}



//.. PRE(old_select, MayBlock)
//.. {
//..    /* struct sel_arg_struct {
//..       unsigned long n;
//..       fd_set *inp, *outp, *exp;
//..       struct timeval *tvp;
//..       };
//..    */
//..    PRE_REG_READ1(long, "old_select", struct sel_arg_struct *, args);
//..    PRE_MEM_READ( "old_select(args)", ARG1, 5*sizeof(UWord) );
//.. 
//..    {
//..       UInt* arg_struct = (UInt*)ARG1;
//..       UInt a1, a2, a3, a4, a5;
//.. 
//..       a1 = arg_struct[0];
//..       a2 = arg_struct[1];
//..       a3 = arg_struct[2];
//..       a4 = arg_struct[3];
//..       a5 = arg_struct[4];
//.. 
//..       PRINT("old_select ( %d, %p, %p, %p, %p )", a1,a2,a3,a4,a5);
//..       if (a2 != (Addr)NULL)
//.. 	 PRE_MEM_READ( "old_select(readfds)",   a2, a1/8 /* __FD_SETSIZE/8 */ );
//..       if (a3 != (Addr)NULL)
//.. 	 PRE_MEM_READ( "old_select(writefds)",  a3, a1/8 /* __FD_SETSIZE/8 */ );
//..       if (a4 != (Addr)NULL)
//.. 	 PRE_MEM_READ( "old_select(exceptfds)", a4, a1/8 /* __FD_SETSIZE/8 */ );
//..       if (a5 != (Addr)NULL)
//.. 	 PRE_MEM_READ( "old_select(timeout)", a5, sizeof(struct vki_timeval) );
//..    }
//.. }

//.. PRE(sys_clone, Special)
//.. {
//..    UInt cloneflags;
//.. 
//..    PRINT("sys_clone ( %x, %p, %p, %p, %p )",ARG1,ARG2,ARG3,ARG4,ARG5);
//..    PRE_REG_READ5(int, "clone",
//..                  unsigned long, flags,
//..                  void *, child_stack,
//..                  int *, parent_tidptr,
//..                  vki_modify_ldt_t *, tlsinfo,
//..                  int *, child_tidptr);
//.. 
//..    if (ARG1 & VKI_CLONE_PARENT_SETTID) {
//..       PRE_MEM_WRITE("clone(parent_tidptr)", ARG3, sizeof(Int));
//..       if (!VG_(is_addressable)(ARG3, sizeof(Int), VKI_PROT_WRITE)) {
//..          SET_RESULT( -VKI_EFAULT );
//..          return;
//..       }
//..    }
//..    if (ARG1 & (VKI_CLONE_CHILD_SETTID | VKI_CLONE_CHILD_CLEARTID)) {
//..       PRE_MEM_WRITE("clone(child_tidptr)", ARG5, sizeof(Int));
//..       if (!VG_(is_addressable)(ARG5, sizeof(Int), VKI_PROT_WRITE)) {
//..          SET_RESULT( -VKI_EFAULT );
//..          return;
//..       }
//..    }
//..    if (ARG1 & VKI_CLONE_SETTLS) {
//..       PRE_MEM_READ("clone(tls_user_desc)", ARG4, sizeof(vki_modify_ldt_t));
//..       if (!VG_(is_addressable)(ARG4, sizeof(vki_modify_ldt_t), VKI_PROT_READ)) {
//..          SET_RESULT( -VKI_EFAULT );
//..          return;
//..       }
//..    }
//.. 
//..    cloneflags = ARG1;
//.. 
//..    if (!VG_(client_signal_OK)(ARG1 & VKI_CSIGNAL)) {
//..       SET_RESULT( -VKI_EINVAL );
//..       return;
//..    }
//.. 
//..    /* Only look at the flags we really care about */
//..    switch(cloneflags & (VKI_CLONE_VM | VKI_CLONE_FS | VKI_CLONE_FILES | VKI_CLONE_VFORK)) {
//..    case VKI_CLONE_VM | VKI_CLONE_FS | VKI_CLONE_FILES:
//..       /* thread creation */
//..       SET_RESULT(do_clone(tid,
//..                           ARG1,         /* flags */
//..                           (Addr)ARG2,   /* child ESP */
//..                           (Int *)ARG3,  /* parent_tidptr */
//..                           (Int *)ARG5,  /* child_tidptr */
//..                           (vki_modify_ldt_t *)ARG4)); /* set_tls */
//..       break;
//.. 
//..    case VKI_CLONE_VFORK | VKI_CLONE_VM: /* vfork */
//..       /* FALLTHROUGH - assume vfork == fork */
//..       cloneflags &= ~(VKI_CLONE_VFORK | VKI_CLONE_VM);
//.. 
//..    case 0: /* plain fork */
//..       SET_RESULT(do_fork_clone(tid,
//..                                cloneflags,              /* flags */
//..                                (Addr)ARG2,      /* child ESP */
//..                                (Int *)ARG3,     /* parent_tidptr */
//..                                (Int *)ARG5));   /* child_tidptr */
//..       break;
//.. 
//..    default:
//..       /* should we just ENOSYS? */
//..       VG_(message)(Vg_UserMsg, "Unsupported clone() flags: %x", ARG1);
//..       VG_(unimplemented)
//..          ("Valgrind does not support general clone().  The only supported uses "
//..           "are via a threads library, fork, or vfork.");
//..    }
//.. 
//..    if (!VG_(is_kerror)(RES)) {
//..       if (ARG1 & VKI_CLONE_PARENT_SETTID)
//..          POST_MEM_WRITE(ARG3, sizeof(Int));
//..       if (ARG1 & (VKI_CLONE_CHILD_SETTID | VKI_CLONE_CHILD_CLEARTID))
//..          POST_MEM_WRITE(ARG5, sizeof(Int));
//.. 
//..       /* Thread creation was successful; let the child have the chance
//..          to run */
//..       VG_(vg_yield)();
//..    }
//.. }

//.. PRE(sys_sigreturn, Special)
//.. {
//..    PRINT("sigreturn ( )");
//.. 
//..    /* Adjust esp to point to start of frame; skip back up over
//..       sigreturn sequence's "popl %eax" and handler ret addr */
//..    tst->arch.vex.guest_ESP -= sizeof(Addr)+sizeof(Word);
//.. 
//..    /* This is only so that the EIP is (might be) useful to report if
//..       something goes wrong in the sigreturn */
//..    VGA_(restart_syscall)(&tst->arch);
//.. 
//..    VG_(sigframe_destroy)(tid, False);
//.. 
//..    /* Keep looking for signals until there are none */
//..    VG_(poll_signals)(tid);
//.. 
//..    /* placate return-must-be-set assertion */
//..    SET_RESULT(RES);
//.. }

//.. PRE(sys_rt_sigreturn, Special)
//.. {
//..    PRINT("rt_sigreturn ( )");
//.. 
//..    /* Adjust esp to point to start of frame; skip back up over handler
//..       ret addr */
//..    tst->arch.vex.guest_ESP -= sizeof(Addr);
//.. 
//..    /* This is only so that the EIP is (might be) useful to report if
//..       something goes wrong in the sigreturn */
//..    VGA_(restart_syscall)(&tst->arch);
//.. 
//..    VG_(sigframe_destroy)(tid, False);
//.. 
//..    /* Keep looking for signals until there are none */
//..    VG_(poll_signals)(tid);
//.. 
//..    /* placate return-must-be-set assertion */
//..    SET_RESULT(RES);
//.. }

//.. PRE(sys_modify_ldt, Special)
//.. {
//..    PRINT("sys_modify_ldt ( %d, %p, %d )", ARG1,ARG2,ARG3);
//..    PRE_REG_READ3(int, "modify_ldt", int, func, void *, ptr,
//..                  unsigned long, bytecount);
//..    
//..    if (ARG1 == 0) {
//..       /* read the LDT into ptr */
//..       PRE_MEM_WRITE( "modify_ldt(ptr)", ARG2, ARG3 );
//..    }
//..    if (ARG1 == 1 || ARG1 == 0x11) {
//..       /* write the LDT with the entry pointed at by ptr */
//..       PRE_MEM_READ( "modify_ldt(ptr)", ARG2, sizeof(vki_modify_ldt_t) );
//..    }
//..    /* "do" the syscall ourselves; the kernel never sees it */
//..    SET_RESULT( VG_(sys_modify_ldt)( tid, ARG1, (void*)ARG2, ARG3 ) );
//.. 
//..    if (ARG1 == 0 && !VG_(is_kerror)(RES) && RES > 0) {
//..       POST_MEM_WRITE( ARG2, RES );
//..    }
//.. }

//.. PRE(sys_set_thread_area, Special)
//.. {
//..    PRINT("sys_set_thread_area ( %p )", ARG1);
//..    PRE_REG_READ1(int, "set_thread_area", struct user_desc *, u_info)
//..    PRE_MEM_READ( "set_thread_area(u_info)", ARG1, sizeof(vki_modify_ldt_t) );
//.. 
//..    /* "do" the syscall ourselves; the kernel never sees it */
//..    SET_RESULT( VG_(sys_set_thread_area)( tid, (void *)ARG1 ) );
//.. }

//.. PRE(sys_get_thread_area, Special)
//.. {
//..    PRINT("sys_get_thread_area ( %p )", ARG1);
//..    PRE_REG_READ1(int, "get_thread_area", struct user_desc *, u_info)
//..    PRE_MEM_WRITE( "get_thread_area(u_info)", ARG1, sizeof(vki_modify_ldt_t) );
//.. 
//..    /* "do" the syscall ourselves; the kernel never sees it */
//..    SET_RESULT( VG_(sys_get_thread_area)( tid, (void *)ARG1 ) );
//.. 
//..    if (!VG_(is_kerror)(RES)) {
//..       POST_MEM_WRITE( ARG1, sizeof(vki_modify_ldt_t) );
//..    }
//.. }

//.. // Parts of this are ppc32-specific, but the *PEEK* cases are generic.
//.. // XXX: Why is the memory pointed to by ARG3 never checked?
//.. PRE(sys_ptrace, 0)
//.. {
//..    PRINT("sys_ptrace ( %d, %d, %p, %p )", ARG1,ARG2,ARG3,ARG4);
//..    PRE_REG_READ4(int, "ptrace", 
//..                  long, request, long, pid, long, addr, long, data);
//..    switch (ARG1) {
//..    case VKI_PTRACE_PEEKTEXT:
//..    case VKI_PTRACE_PEEKDATA:
//..    case VKI_PTRACE_PEEKUSR:
//..       PRE_MEM_WRITE( "ptrace(peek)", ARG4, 
//.. 		     sizeof (long));
//..       break;
//..    case VKI_PTRACE_GETREGS:
//..       PRE_MEM_WRITE( "ptrace(getregs)", ARG4, 
//.. 		     sizeof (struct vki_user_regs_struct));
//..       break;
//..    case VKI_PTRACE_GETFPREGS:
//..       PRE_MEM_WRITE( "ptrace(getfpregs)", ARG4, 
//.. 		     sizeof (struct vki_user_i387_struct));
//..       break;
//..    case VKI_PTRACE_GETFPXREGS:
//..       PRE_MEM_WRITE( "ptrace(getfpxregs)", ARG4, 
//..                      sizeof(struct vki_user_fxsr_struct) );
//..       break;
//..    case VKI_PTRACE_SETREGS:
//..       PRE_MEM_READ( "ptrace(setregs)", ARG4, 
//.. 		     sizeof (struct vki_user_regs_struct));
//..       break;
//..    case VKI_PTRACE_SETFPREGS:
//..       PRE_MEM_READ( "ptrace(setfpregs)", ARG4, 
//.. 		     sizeof (struct vki_user_i387_struct));
//..       break;
//..    case VKI_PTRACE_SETFPXREGS:
//..       PRE_MEM_READ( "ptrace(setfpxregs)", ARG4, 
//..                      sizeof(struct vki_user_fxsr_struct) );
//..       break;
//..    default:
//..       break;
//..    }
//.. }

//.. POST(sys_ptrace)
//.. {
//..    switch (ARG1) {
//..    case VKI_PTRACE_PEEKTEXT:
//..    case VKI_PTRACE_PEEKDATA:
//..    case VKI_PTRACE_PEEKUSR:
//..       POST_MEM_WRITE( ARG4, sizeof (long));
//..       break;
//..    case VKI_PTRACE_GETREGS:
//..       POST_MEM_WRITE( ARG4, sizeof (struct vki_user_regs_struct));
//..       break;
//..    case VKI_PTRACE_GETFPREGS:
//..       POST_MEM_WRITE( ARG4, sizeof (struct vki_user_i387_struct));
//..       break;
//..    case VKI_PTRACE_GETFPXREGS:
//..       POST_MEM_WRITE( ARG4, sizeof(struct vki_user_fxsr_struct) );
//..       break;
//..    default:
//..       break;
//..    }
//.. }

//.. // XXX: this duplicates a function in coregrind/vg_syscalls.c, yuk
//.. static Addr deref_Addr ( ThreadId tid, Addr a, Char* s )
//.. {
//..    Addr* a_p = (Addr*)a;
//..    PRE_MEM_READ( s, (Addr)a_p, sizeof(Addr) );
//..    return *a_p;
//.. }

//.. // XXX: should use the constants here (eg. SHMAT), not the numbers directly!
//.. PRE(sys_ipc, 0)
//.. {
//..    PRINT("sys_ipc ( %d, %d, %d, %d, %p, %d )", ARG1,ARG2,ARG3,ARG4,ARG5,ARG6);
//..    // XXX: this is simplistic -- some args are not used in all circumstances.
//..    PRE_REG_READ6(int, "ipc",
//..                  vki_uint, call, int, first, int, second, int, third,
//..                  void *, ptr, long, fifth)
//.. 
//..    switch (ARG1 /* call */) {
//..    case VKI_SEMOP:
//..       VG_(generic_PRE_sys_semop)( tid, ARG2, ARG5, ARG3 );
//..       /* tst->sys_flags |= MayBlock; */
//..       break;
//..    case VKI_SEMGET:
//..       break;
//..    case VKI_SEMCTL:
//..    {
//..       UWord arg = deref_Addr( tid, ARG5, "semctl(arg)" );
//..       VG_(generic_PRE_sys_semctl)( tid, ARG2, ARG3, ARG4, arg );
//..       break;
//..    }
//..    case VKI_SEMTIMEDOP:
//..       VG_(generic_PRE_sys_semtimedop)( tid, ARG2, ARG5, ARG3, ARG6 );
//..       /* tst->sys_flags |= MayBlock; */
//..       break;
//..    case VKI_MSGSND:
//..       VG_(generic_PRE_sys_msgsnd)( tid, ARG2, ARG5, ARG3, ARG4 );
//..       /* if ((ARG4 & VKI_IPC_NOWAIT) == 0)
//..             tst->sys_flags |= MayBlock;
//..       */
//..       break;
//..    case VKI_MSGRCV:
//..    {
//..       Addr msgp;
//..       Word msgtyp;
//..  
//..       msgp = deref_Addr( tid,
//.. 			 (Addr) (&((struct vki_ipc_kludge *)ARG5)->msgp),
//.. 			 "msgrcv(msgp)" );
//..       msgtyp = deref_Addr( tid,
//.. 			   (Addr) (&((struct vki_ipc_kludge *)ARG5)->msgtyp),
//.. 			   "msgrcv(msgp)" );
//.. 
//..       VG_(generic_PRE_sys_msgrcv)( tid, ARG2, msgp, ARG3, msgtyp, ARG4 );
//.. 
//..       /* if ((ARG4 & VKI_IPC_NOWAIT) == 0)
//..             tst->sys_flags |= MayBlock;
//..       */
//..       break;
//..    }
//..    case VKI_MSGGET:
//..       break;
//..    case VKI_MSGCTL:
//..       VG_(generic_PRE_sys_msgctl)( tid, ARG2, ARG3, ARG5 );
//..       break;
//..    case VKI_SHMAT:
//..       PRE_MEM_WRITE( "shmat(raddr)", ARG4, sizeof(Addr) );
//..       ARG5 = VG_(generic_PRE_sys_shmat)( tid, ARG2, ARG5, ARG3 );
//..       if (ARG5 == 0)
//..          SET_RESULT( -VKI_EINVAL );
//..       break;
//..    case VKI_SHMDT:
//..       if (!VG_(generic_PRE_sys_shmdt)(tid, ARG5))
//.. 	 SET_RESULT( -VKI_EINVAL );
//..       break;
//..    case VKI_SHMGET:
//..       break;
//..    case VKI_SHMCTL: /* IPCOP_shmctl */
//..       VG_(generic_PRE_sys_shmctl)( tid, ARG2, ARG3, ARG5 );
//..       break;
//..    default:
//..       VG_(message)(Vg_DebugMsg, "FATAL: unhandled syscall(ipc) %d", ARG1 );
//..       VG_(core_panic)("... bye!\n");
//..       break; /*NOTREACHED*/
//..    }   
//.. }

//.. POST(sys_ipc)
//.. {
//..    switch (ARG1 /* call */) {
//..    case VKI_SEMOP:
//..    case VKI_SEMGET:
//..       break;
//..    case VKI_SEMCTL:
//..    {
//..       UWord arg = deref_Addr( tid, ARG5, "semctl(arg)" );
//..       VG_(generic_PRE_sys_semctl)( tid, ARG2, ARG3, ARG4, arg );
//..       break;
//..    }
//..    case VKI_SEMTIMEDOP:
//..    case VKI_MSGSND:
//..       break;
//..    case VKI_MSGRCV:
//..    {
//..       Addr msgp;
//..       Word msgtyp;
//.. 
//..       msgp = deref_Addr( tid,
//.. 			 (Addr) (&((struct vki_ipc_kludge *)ARG5)->msgp),
//.. 			 "msgrcv(msgp)" );
//..       msgtyp = deref_Addr( tid,
//.. 			   (Addr) (&((struct vki_ipc_kludge *)ARG5)->msgtyp),
//.. 			   "msgrcv(msgp)" );
//.. 
//..       VG_(generic_POST_sys_msgrcv)( tid, RES, ARG2, msgp, ARG3, msgtyp, ARG4 );
//..       break;
//..    }
//..    case VKI_MSGGET:
//..       break;
//..    case VKI_MSGCTL:
//..       VG_(generic_POST_sys_msgctl)( tid, RES, ARG2, ARG3, ARG5 );
//..       break;
//..    case VKI_SHMAT:
//..    {
//..       Addr addr;
//.. 
//..       /* force readability. before the syscall it is
//..        * indeed uninitialized, as can be seen in
//..        * glibc/sysdeps/unix/sysv/linux/shmat.c */
//..       POST_MEM_WRITE( ARG4, sizeof( Addr ) );
//.. 
//..       addr = deref_Addr ( tid, ARG4, "shmat(addr)" );
//..       if ( addr > 0 ) { 
//..          VG_(generic_POST_sys_shmat)( tid, addr, ARG2, ARG5, ARG3 );
//..       }
//..       break;
//..    }
//..    case VKI_SHMDT:
//..       VG_(generic_POST_sys_shmdt)( tid, RES, ARG5 );
//..       break;
//..    case VKI_SHMGET:
//..       break;
//..    case VKI_SHMCTL:
//..       VG_(generic_POST_sys_shmctl)( tid, RES, ARG2, ARG3, ARG5 );
//..       break;
//..    default:
//..       VG_(message)(Vg_DebugMsg,
//.. 		   "FATAL: unhandled syscall(ipc) %d",
//.. 		   ARG1 );
//..       VG_(core_panic)("... bye!\n");
//..       break; /*NOTREACHED*/
//..    }
//.. }


//.. // jrs 20050207: this is from the svn branch
//.. //PRE(sys_sigaction, Special)
//.. //{
//.. //   PRINT("sys_sigaction ( %d, %p, %p )", ARG1,ARG2,ARG3);
//.. //   PRE_REG_READ3(int, "sigaction",
//.. //                 int, signum, const struct old_sigaction *, act,
//.. //                 struct old_sigaction *, oldact)
//.. //   if (ARG2 != 0)
//.. //      PRE_MEM_READ( "sigaction(act)", ARG2, sizeof(struct vki_old_sigaction));
//.. //   if (ARG3 != 0)
//.. //      PRE_MEM_WRITE( "sigaction(oldact)", ARG3, sizeof(struct vki_old_sigaction));
//.. //
//.. //   VG_(do_sys_sigaction)(tid);
//.. //}

//.. /* Convert from non-RT to RT sigset_t's */
//.. static void convert_sigset_to_rt(const vki_old_sigset_t *oldset, vki_sigset_t *set)
//.. {
//..    VG_(sigemptyset)(set);
//..    set->sig[0] = *oldset;
//.. }
//.. PRE(sys_sigaction, Special)
//.. {
//..    struct vki_sigaction new, old;
//..    struct vki_sigaction *newp, *oldp;
//.. 
//..    PRINT("sys_sigaction ( %d, %p, %p )", ARG1,ARG2,ARG3);
//..    PRE_REG_READ3(int, "sigaction",
//..                  int, signum, const struct old_sigaction *, act,
//..                  struct old_sigaction *, oldact);
//.. 
//..    newp = oldp = NULL;
//.. 
//..    if (ARG2 != 0)
//..       PRE_MEM_READ( "sigaction(act)", ARG2, sizeof(struct vki_old_sigaction));
//.. 
//..    if (ARG3 != 0) {
//..       PRE_MEM_WRITE( "sigaction(oldact)", ARG3, sizeof(struct vki_old_sigaction));
//..       oldp = &old;
//..    }
//.. 
//..    //jrs 20050207: what?!  how can this make any sense?
//..    //if (VG_(is_kerror)(SYSRES))
//..    //   return;
//.. 
//..    if (ARG2 != 0) {
//..       struct vki_old_sigaction *oldnew = (struct vki_old_sigaction *)ARG2;
//.. 
//..       new.ksa_handler = oldnew->ksa_handler;
//..       new.sa_flags = oldnew->sa_flags;
//..       new.sa_restorer = oldnew->sa_restorer;
//..       convert_sigset_to_rt(&oldnew->sa_mask, &new.sa_mask);
//..       newp = &new;
//..    }
//.. 
//..    SET_RESULT( VG_(do_sys_sigaction)(ARG1, newp, oldp) );
//.. 
//..    if (ARG3 != 0 && RES == 0) {
//..       struct vki_old_sigaction *oldold = (struct vki_old_sigaction *)ARG3;
//.. 
//..       oldold->ksa_handler = oldp->ksa_handler;
//..       oldold->sa_flags = oldp->sa_flags;
//..       oldold->sa_restorer = oldp->sa_restorer;
//..       oldold->sa_mask = oldp->sa_mask.sig[0];
//..    }
//.. }

//.. POST(sys_sigaction)
//.. {
//..    if (RES == 0 && ARG3 != 0)
//..       POST_MEM_WRITE( ARG3, sizeof(struct vki_old_sigaction));
//.. }

#undef PRE
#undef POST


/* ---------------------------------------------------------------------
   The ppc32/Linux syscall table
   ------------------------------------------------------------------ */

/* Add an ppc32-linux specific wrapper to a syscall table. */
#define PLAX_(sysno, name)    WRAPPER_ENTRY_X_(ppc32_linux, sysno, name) 
#define PLAXY(sysno, name)    WRAPPER_ENTRY_XY(ppc32_linux, sysno, name)

// This table maps from __NR_xxx syscall numbers (from
// linux/include/asm-ppc/unistd.h) to the appropriate PRE/POST sys_foo()
// wrappers on ppc32 (as per sys_call_table in linux/arch/ppc/kernel/entry.S).
//
// For those syscalls not handled by Valgrind, the annotation indicate its
// arch/OS combination, eg. */* (generic), */Linux (Linux only), ?/?
// (unknown).

const SyscallTableEntry VGP_(syscall_table)[] = {
//..   (restart_syscall)                                      // 0
   GENX_(__NR_exit,              sys_exit),                   // 1
//..    GENX_(__NR_fork,              sys_fork),              // 2
   GENXY(__NR_read,              sys_read),                   // 3
   GENX_(__NR_write,             sys_write),                  // 4

   GENXY(__NR_open,              sys_open),                   // 5
   GENXY(__NR_close,             sys_close),                  // 6
//..    GENXY(__NR_waitpid,           sys_waitpid),           // 7
//..    GENXY(__NR_creat,             sys_creat),             // 8
//..    GENX_(__NR_link,              sys_link),              // 9
//.. 
//..    GENX_(__NR_unlink,            sys_unlink),            // 10
//..    GENX_(__NR_execve,            sys_execve),            // 11
//..    GENX_(__NR_chdir,             sys_chdir),             // 12
//..    GENXY(__NR_time,              sys_time),              // 13
//..    GENX_(__NR_mknod,             sys_mknod),             // 14
//.. 
//..    GENX_(__NR_chmod,             sys_chmod),             // 15
//..    //   (__NR_lchown,            sys_lchown16),          // 16 ## P
//..    GENX_(__NR_break,             sys_ni_syscall),        // 17
//..    //   (__NR_oldstat,           sys_stat),              // 18 (obsolete)
//..    GENX_(__NR_lseek,             sys_lseek),             // 19
//.. 
//..    GENX_(__NR_getpid,            sys_getpid),            // 20
//..    LINX_(__NR_mount,             sys_mount),             // 21
//..    LINX_(__NR_umount,            sys_oldumount),         // 22
//..    GENX_(__NR_setuid,            sys_setuid16),          // 23 ## P
//..    GENX_(__NR_getuid,            sys_getuid16),          // 24 ## P
//.. 
//..    //   (__NR_stime,             sys_stime),             // 25 * (SVr4,SVID,X/OPEN)
//..    PLAXY(__NR_ptrace,            sys_ptrace),            // 26
//..    GENX_(__NR_alarm,             sys_alarm),             // 27
//..    //   (__NR_oldfstat,          sys_fstat),             // 28 * L -- obsolete
//..    GENX_(__NR_pause,             sys_pause),             // 29
//.. 
//..    GENX_(__NR_utime,             sys_utime),             // 30
//..    GENX_(__NR_stty,              sys_ni_syscall),        // 31
//..    GENX_(__NR_gtty,              sys_ni_syscall),        // 32
   GENX_(__NR_access,            sys_access),                 // 33
//..    GENX_(__NR_nice,              sys_nice),              // 34
//.. 
//..    GENX_(__NR_ftime,             sys_ni_syscall),        // 35
//..    GENX_(__NR_sync,              sys_sync),              // 36
//..    GENX_(__NR_kill,              sys_kill),              // 37
//..    GENX_(__NR_rename,            sys_rename),            // 38
//..    GENX_(__NR_mkdir,             sys_mkdir),             // 39
//.. 
//..    GENX_(__NR_rmdir,             sys_rmdir),             // 40
//..    GENXY(__NR_dup,               sys_dup),               // 41
//..    GENXY(__NR_pipe,              sys_pipe),              // 42
//..    GENXY(__NR_times,             sys_times),             // 43
//..    GENX_(__NR_prof,              sys_ni_syscall),        // 44
//.. 
   GENX_(__NR_brk,               sys_brk),                    // 45
//..    GENX_(__NR_setgid,            sys_setgid16),          // 46
//..    GENX_(__NR_getgid,            sys_getgid16),          // 47
//..    //   (__NR_signal,            sys_signal),            // 48 */* (ANSI C)
//..    GENX_(__NR_geteuid,           sys_geteuid16),         // 49
//.. 
//..    GENX_(__NR_getegid,           sys_getegid16),         // 50
//..    GENX_(__NR_acct,              sys_acct),              // 51
//..    LINX_(__NR_umount2,           sys_umount),            // 52
//..    GENX_(__NR_lock,              sys_ni_syscall),        // 53
   GENXY(__NR_ioctl,             sys_ioctl),             // 54
//.. 
//..    GENXY(__NR_fcntl,             sys_fcntl),             // 55
//..    GENX_(__NR_mpx,               sys_ni_syscall),        // 56
//..    GENX_(__NR_setpgid,           sys_setpgid),           // 57
//..    GENX_(__NR_ulimit,            sys_ni_syscall),        // 58
//..    //   (__NR_oldolduname,       sys_olduname),          // 59 Linux -- obsolete
//.. 
//..    GENX_(__NR_umask,             sys_umask),             // 60
//..    GENX_(__NR_chroot,            sys_chroot),            // 61
//..    //   (__NR_ustat,             sys_ustat)              // 62 SVr4 -- deprecated
//..    GENXY(__NR_dup2,              sys_dup2),              // 63
//..    GENXY(__NR_getppid,           sys_getppid),           // 64
//.. 
//..    GENX_(__NR_getpgrp,           sys_getpgrp),           // 65
//..    GENX_(__NR_setsid,            sys_setsid),            // 66
//..    PLAXY(__NR_sigaction,         sys_sigaction),         // 67
//..    //   (__NR_sgetmask,          sys_sgetmask),          // 68 */* (ANSI C)
//..    //   (__NR_ssetmask,          sys_ssetmask),          // 69 */* (ANSI C)
//.. 
//..    GENX_(__NR_setreuid,          sys_setreuid16),        // 70
//..    GENX_(__NR_setregid,          sys_setregid16),        // 71
//..    GENX_(__NR_sigsuspend,        sys_sigsuspend),        // 72
//..    GENXY(__NR_sigpending,        sys_sigpending),        // 73
//..    //   (__NR_sethostname,       sys_sethostname),       // 74 */*
//.. 
//..    GENX_(__NR_setrlimit,         sys_setrlimit),         // 75
//..    GENXY(__NR_getrlimit,         sys_old_getrlimit),     // 76
//..    GENXY(__NR_getrusage,         sys_getrusage),         // 77
//..    GENXY(__NR_gettimeofday,      sys_gettimeofday),      // 78
//..    GENX_(__NR_settimeofday,      sys_settimeofday),      // 79
//.. 
//..    GENXY(__NR_getgroups,         sys_getgroups16),       // 80
//..    GENX_(__NR_setgroups,         sys_setgroups16),       // 81
//..    PLAX_(__NR_select,            old_select),            // 82
//..    GENX_(__NR_symlink,           sys_symlink),           // 83
//..    //   (__NR_oldlstat,          sys_lstat),             // 84 -- obsolete
//.. 
//..    GENX_(__NR_readlink,          sys_readlink),          // 85
//..    //   (__NR_uselib,            sys_uselib),            // 86 */Linux
//..    //   (__NR_swapon,            sys_swapon),            // 87 */Linux
//..    //   (__NR_reboot,            sys_reboot),            // 88 */Linux
//..    //   (__NR_readdir,           old_readdir),           // 89 -- superseded

   GENXY(__NR_mmap,              sys_mmap2),                  // 90
   GENXY(__NR_munmap,            sys_munmap),            // 91
//..    GENX_(__NR_truncate,          sys_truncate),          // 92
//..    GENX_(__NR_ftruncate,         sys_ftruncate),         // 93
//..    GENX_(__NR_fchmod,            sys_fchmod),            // 94
//.. 
//..    GENX_(__NR_fchown,            sys_fchown16),          // 95
//..    GENX_(__NR_getpriority,       sys_getpriority),       // 96
//..    GENX_(__NR_setpriority,       sys_setpriority),       // 97
//..    GENX_(__NR_profil,            sys_ni_syscall),        // 98
//..    GENXY(__NR_statfs,            sys_statfs),            // 99
//.. 
//..    GENXY(__NR_fstatfs,           sys_fstatfs),           // 100
//..    LINX_(__NR_ioperm,            sys_ioperm),            // 101
//..    GENXY(__NR_socketcall,        sys_socketcall),        // 102
//..    LINXY(__NR_syslog,            sys_syslog),            // 103
//..    GENXY(__NR_setitimer,         sys_setitimer),         // 104
//.. 
//..    GENXY(__NR_getitimer,         sys_getitimer),         // 105
   GENXY(__NR_stat,              sys_newstat),                // 106
//..    GENXY(__NR_lstat,             sys_newlstat),          // 107
//..    GENXY(__NR_fstat,             sys_newfstat),          // 108
//..    //   (__NR_olduname,          sys_uname),             // 109 -- obsolete
//.. 
//..    GENX_(__NR_iopl,              sys_iopl),              // 110
//..    LINX_(__NR_vhangup,           sys_vhangup),           // 111
//..    GENX_(__NR_idle,              sys_ni_syscall),        // 112
//..    //   (__NR_vm86old,           sys_vm86old),           // 113 x86/Linux-only
//..    GENXY(__NR_wait4,             sys_wait4),             // 114
//.. 
//..    //   (__NR_swapoff,           sys_swapoff),           // 115 */Linux 
//..    LINXY(__NR_sysinfo,           sys_sysinfo),           // 116
//..    PLAXY(__NR_ipc,               sys_ipc),               // 117
//..    GENX_(__NR_fsync,             sys_fsync),             // 118
//..    PLAX_(__NR_sigreturn,         sys_sigreturn),         // 119 ?/Linux
//.. 
//..    PLAX_(__NR_clone,             sys_clone),             // 120
//..    //   (__NR_setdomainname,     sys_setdomainname),     // 121 */*(?)
   GENXY(__NR_uname,             sys_newuname),               // 122
//..    PLAX_(__NR_modify_ldt,        sys_modify_ldt),        // 123
//..    LINXY(__NR_adjtimex,          sys_adjtimex),          // 124
//.. 
   GENXY(__NR_mprotect,          sys_mprotect),               // 125
//..    GENXY(__NR_sigprocmask,       sys_sigprocmask),       // 126
//..    // Nb: create_module() was removed 2.4-->2.6
//..    GENX_(__NR_create_module,     sys_ni_syscall),        // 127
//..    GENX_(__NR_init_module,       sys_init_module),       // 128
//..    //   (__NR_delete_module,     sys_delete_module),     // 129 (*/Linux)?
//.. 
//..    // Nb: get_kernel_syms() was removed 2.4-->2.6
//..    GENX_(__NR_get_kernel_syms,   sys_ni_syscall),        // 130
//..    GENX_(__NR_quotactl,          sys_quotactl),          // 131
//..    GENX_(__NR_getpgid,           sys_getpgid),           // 132
//..    GENX_(__NR_fchdir,            sys_fchdir),            // 133
//..    //   (__NR_bdflush,           sys_bdflush),           // 134 */Linux
//.. 
//..    //   (__NR_sysfs,             sys_sysfs),             // 135 SVr4
//..    LINX_(__NR_personality,       sys_personality),       // 136
//..    GENX_(__NR_afs_syscall,       sys_ni_syscall),        // 137
//..    LINX_(__NR_setfsuid,          sys_setfsuid16),        // 138
//..    LINX_(__NR_setfsgid,          sys_setfsgid16),        // 139
//.. 
//..    LINXY(__NR__llseek,           sys_llseek),            // 140
//..    GENXY(__NR_getdents,          sys_getdents),          // 141
//..    GENX_(__NR__newselect,        sys_select),            // 142
//..    GENX_(__NR_flock,             sys_flock),             // 143
//..    GENX_(__NR_msync,             sys_msync),             // 144
//.. 
//..    GENXY(__NR_readv,             sys_readv),             // 145
   GENX_(__NR_writev,            sys_writev),                 // 146
//..    GENX_(__NR_getsid,            sys_getsid),            // 147
//..    GENX_(__NR_fdatasync,         sys_fdatasync),         // 148
   LINXY(__NR__sysctl,           sys_sysctl),            // 149
//.. 
//..    GENX_(__NR_mlock,             sys_mlock),             // 150
//..    GENX_(__NR_munlock,           sys_munlock),           // 151
//..    GENX_(__NR_mlockall,          sys_mlockall),          // 152
//..    GENX_(__NR_munlockall,        sys_munlockall),        // 153
//..    GENXY(__NR_sched_setparam,    sys_sched_setparam),    // 154
//.. 
//..    GENXY(__NR_sched_getparam,         sys_sched_getparam),        // 155
//..    GENX_(__NR_sched_setscheduler,     sys_sched_setscheduler),    // 156
//..    GENX_(__NR_sched_getscheduler,     sys_sched_getscheduler),    // 157
//..    GENX_(__NR_sched_yield,            sys_sched_yield),           // 158
//..    GENX_(__NR_sched_get_priority_max, sys_sched_get_priority_max),// 159
//.. 
//..    GENX_(__NR_sched_get_priority_min, sys_sched_get_priority_min),// 160
//..    //   (__NR_sched_rr_get_interval,  sys_sched_rr_get_interval), // 161 */*
//..    GENXY(__NR_nanosleep,         sys_nanosleep),         // 162
//..    GENX_(__NR_mremap,            sys_mremap),            // 163
//..    LINX_(__NR_setresuid,         sys_setresuid16),       // 164
//.. 
//..    LINXY(__NR_getresuid,         sys_getresuid16),       // 165

//..    GENX_(__NR_query_module,      sys_ni_syscall),        // 166
//..    GENXY(__NR_poll,              sys_poll),              // 167
//..    //   (__NR_nfsservctl,        sys_nfsservctl),        // 168 */Linux
//.. 
//..    LINX_(__NR_setresgid,         sys_setresgid16),       // 169
//..    LINXY(__NR_getresgid,         sys_getresgid16),       // 170
//..    LINX_(__NR_prctl,             sys_prctl),             // 171
//..    PLAX_(__NR_rt_sigreturn,      sys_rt_sigreturn),      // 172
   GENXY(__NR_rt_sigaction,      sys_rt_sigaction),      // 173

   GENXY(__NR_rt_sigprocmask,    sys_rt_sigprocmask),    // 174
//..    GENXY(__NR_rt_sigpending,     sys_rt_sigpending),     // 175
//..    GENXY(__NR_rt_sigtimedwait,   sys_rt_sigtimedwait),   // 176
//..    GENXY(__NR_rt_sigqueueinfo,   sys_rt_sigqueueinfo),   // 177
//..    GENX_(__NR_rt_sigsuspend,     sys_rt_sigsuspend),     // 178
//.. 
//..    GENXY(__NR_pread64,           sys_pread64),           // 179
//..    GENX_(__NR_pwrite64,          sys_pwrite64),          // 180
//..    GENX_(__NR_chown,             sys_chown16),           // 181
//..    GENXY(__NR_getcwd,            sys_getcwd),            // 182
//..    GENXY(__NR_capget,            sys_capget),            // 183
//.. 
//..    GENX_(__NR_capset,            sys_capset),            // 184
//..    GENXY(__NR_sigaltstack,       sys_sigaltstack),       // 185
//..    LINXY(__NR_sendfile,          sys_sendfile),          // 186
//..    GENXY(__NR_getpmsg,           sys_getpmsg),           // 187
//..    GENX_(__NR_putpmsg,           sys_putpmsg),           // 188
//.. 
//..    // Nb: we treat vfork as fork
//..    GENX_(__NR_vfork,             sys_fork),              // 189
   GENXY(__NR_ugetrlimit,        sys_getrlimit),         // 190
//__NR_readahead      // 191 ppc/Linux only?
   GENXY(__NR_mmap2,             sys_mmap2),             // 192
//..    GENX_(__NR_truncate64,        sys_truncate64),        // 193
//..    GENX_(__NR_ftruncate64,       sys_ftruncate64),       // 194
//..    

   PLAXY(__NR_stat64,            sys_stat64),                 // 195
//..    GENXY(__NR_lstat64,           sys_lstat64),           // 196
   PLAXY(__NR_fstat64,           sys_fstat64),                // 197

// __NR_pciconfig_read                                        // 198
// __NR_pciconfig_write                                       // 199
// __NR_pciconfig_iobase                                      // 200
// __NR_multiplexer                                           // 201

   GENXY(__NR_getdents64,        sys_getdents64),        // 202
//..    //   (__NR_pivot_root,        sys_pivot_root),        // 203 */Linux
   GENXY(__NR_fcntl64,           sys_fcntl64),           // 204
//..    GENX_(__NR_madvise,           sys_madvise),           // 205
//..    GENXY(__NR_mincore,           sys_mincore),           // 206
//..    LINX_(__NR_gettid,            sys_gettid),            // 207
//..    LINX_(__NR_tkill,             sys_tkill),             // 208 */Linux
//..    GENX_(__NR_setxattr,          sys_setxattr),          // 209
//..    GENX_(__NR_lsetxattr,         sys_lsetxattr),         // 210
//..    GENX_(__NR_fsetxattr,         sys_fsetxattr),         // 211
//..    GENXY(__NR_getxattr,          sys_getxattr),          // 212
//..    GENXY(__NR_lgetxattr,         sys_lgetxattr),         // 213
//..    GENXY(__NR_fgetxattr,         sys_fgetxattr),         // 214
//..    GENXY(__NR_listxattr,         sys_listxattr),         // 215
//..    GENXY(__NR_llistxattr,        sys_llistxattr),        // 216
//..    GENXY(__NR_flistxattr,        sys_flistxattr),        // 217
//..    GENX_(__NR_removexattr,       sys_removexattr),       // 218
//..    GENX_(__NR_lremovexattr,      sys_lremovexattr),      // 219
//..    GENX_(__NR_fremovexattr,      sys_fremovexattr),      // 220

   LINXY(__NR_futex,             sys_futex),             // 221
//..    GENX_(__NR_sched_setaffinity, sys_sched_setaffinity), // 222
//..    GENXY(__NR_sched_getaffinity, sys_sched_getaffinity), // 223
/* 224 currently unused */

// __NR_tuxcall                                               // 225

//..    LINXY(__NR_sendfile64,        sys_sendfile64),        // 226
//.. 
//..    LINX_(__NR_io_setup,          sys_io_setup),          // 227
//..    LINX_(__NR_io_destroy,        sys_io_destroy),        // 228
//..    LINXY(__NR_io_getevents,      sys_io_getevents),      // 229
//..    LINX_(__NR_io_submit,         sys_io_submit),         // 230
//..    LINXY(__NR_io_cancel,         sys_io_cancel),         // 231
//.. 
   LINX_(__NR_set_tid_address,   sys_set_tid_address),   // 232

//..    LINX_(__NR_fadvise64,         sys_fadvise64),         // 233 */(Linux?)
   LINX_(__NR_exit_group,        sys_exit_group),             // 234
//..    GENXY(__NR_lookup_dcookie,    sys_lookup_dcookie),    // 235
//..    LINXY(__NR_epoll_create,      sys_epoll_create),      // 236
//..    LINX_(__NR_epoll_ctl,         sys_epoll_ctl),         // 237
//..    LINXY(__NR_epoll_wait,        sys_epoll_wait),        // 238

//..    //   (__NR_remap_file_pages,  sys_remap_file_pages),  // 239 */Linux
//..    GENXY(__NR_timer_create,      sys_timer_create),      // 240
//..    GENXY(__NR_timer_settime,     sys_timer_settime),     // 241
//..    GENXY(__NR_timer_gettime,     sys_timer_gettime),     // 242
//..    GENX_(__NR_timer_getoverrun,  sys_timer_getoverrun),  // 243
//..    GENX_(__NR_timer_delete,      sys_timer_delete),      // 244
//..    GENX_(__NR_clock_settime,     sys_clock_settime),     // 245
//..    GENXY(__NR_clock_gettime,     sys_clock_gettime),     // 246
//..    GENXY(__NR_clock_getres,      sys_clock_getres),      // 247
//..    //   (__NR_clock_nanosleep,   sys_clock_nanosleep),   // 248

// __NR_swapcontext                                           // 249

//..    LINX_(__NR_tgkill,            sys_tgkill),            // 250 */Linux
//..    GENX_(__NR_utimes,            sys_utimes),            // 251
//..    GENXY(__NR_statfs64,          sys_statfs64),          // 252
//..    GENXY(__NR_fstatfs64,         sys_fstatfs64),         // 253
//..    LINX_(__NR_fadvise64_64,      sys_fadvise64_64),      // 254 */(Linux?)

// __NR_rtas                                                  // 255

/* Number 256 is reserved for sys_debug_setcontext */
/* Number 257 is reserved for vserver */
/* Number 258 is reserved for new sys_remap_file_pages */
/* Number 259 is reserved for new sys_mbind */
/* Number 260 is reserved for new sys_get_mempolicy */
/* Number 261 is reserved for new sys_set_mempolicy */

//..    GENXY(__NR_mq_open,           sys_mq_open),           // 262
//..    GENX_(__NR_mq_unlink,         sys_mq_unlink),         // 263
//..    GENX_(__NR_mq_timedsend,      sys_mq_timedsend),      // 264
//..    GENXY(__NR_mq_timedreceive,   sys_mq_timedreceive),   // 265
//..    GENX_(__NR_mq_notify,         sys_mq_notify),         // 266
//..    GENXY(__NR_mq_getsetattr,     sys_mq_getsetattr),     // 267

// __NR_kexec_load                                            // 268
};

const UInt VGP_(syscall_table_size) = 
            sizeof(VGP_(syscall_table)) / sizeof(VGP_(syscall_table)[0]);

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/

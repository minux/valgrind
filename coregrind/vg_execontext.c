
/*--------------------------------------------------------------------*/
/*--- Storage, and equality on, execution contexts (backtraces).   ---*/
/*---                                              vg_execontext.c ---*/
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

#include "core.h"


/*------------------------------------------------------------*/
/*--- Low-level ExeContext storage.                        ---*/
/*------------------------------------------------------------*/

/* The idea is only to ever store any one context once, so as to save
   space and make exact comparisons faster. */

static ExeContext* vg_ec_list[VG_N_EC_LISTS];

/* Stats only: the number of times the system was searched to locate a
   context. */
static UInt vg_ec_searchreqs;

/* Stats only: the number of full context comparisons done. */
static UInt vg_ec_searchcmps;

/* Stats only: total number of stored contexts. */
static UInt vg_ec_totstored;

/* Number of 2, 4 and (fast) full cmps done. */
static UInt vg_ec_cmp2s;
static UInt vg_ec_cmp4s;
static UInt vg_ec_cmpAlls;


/*------------------------------------------------------------*/
/*--- Exported functions.                                  ---*/
/*------------------------------------------------------------*/


/* Initialise this subsystem. */
static void init_ExeContext_storage ( void )
{
   Int i;
   static Bool init_done = False;
   if (init_done)
      return;
   vg_ec_searchreqs = 0;
   vg_ec_searchcmps = 0;
   vg_ec_totstored = 0;
   vg_ec_cmp2s = 0;
   vg_ec_cmp4s = 0;
   vg_ec_cmpAlls = 0;
   for (i = 0; i < VG_N_EC_LISTS; i++)
      vg_ec_list[i] = NULL;
   init_done = True;
}


/* Print stats. */
void VG_(print_ExeContext_stats) ( void )
{
   init_ExeContext_storage();
   VG_(message)(Vg_DebugMsg, 
      "exectx: %d lists, %d contexts (avg %d per list)",
      VG_N_EC_LISTS, vg_ec_totstored, 
      vg_ec_totstored / VG_N_EC_LISTS 
   );
   VG_(message)(Vg_DebugMsg, 
      "exectx: %d searches, %d full compares (%d per 1000)",
      vg_ec_searchreqs, vg_ec_searchcmps, 
      vg_ec_searchreqs == 0 
         ? 0 
         : (UInt)( (((ULong)vg_ec_searchcmps) * 1000) 
           / ((ULong)vg_ec_searchreqs )) 
   );
   VG_(message)(Vg_DebugMsg, 
      "exectx: %d cmp2, %d cmp4, %d cmpAll",
      vg_ec_cmp2s, vg_ec_cmp4s, vg_ec_cmpAlls 
   );
}


/* Print an ExeContext. */
void VG_(pp_ExeContext) ( ExeContext* e )
{
   init_ExeContext_storage();
   VG_(mini_stack_dump) ( e->ips, VG_(clo_backtrace_size) );
}


/* Compare two ExeContexts, comparing all callers. */
Bool VG_(eq_ExeContext) ( VgRes res, ExeContext* e1, ExeContext* e2 )
{
   if (e1 == NULL || e2 == NULL) 
      return False;
   switch (res) {
   case Vg_LowRes:
      /* Just compare the top two callers. */
      vg_ec_cmp2s++;
      if (e1->ips[0] != e2->ips[0]
          || e1->ips[1] != e2->ips[1]) return False;
      return True;

   case Vg_MedRes:
      /* Just compare the top four callers. */
      vg_ec_cmp4s++;
      if (e1->ips[0] != e2->ips[0]) return False;

      if (VG_(clo_backtrace_size) < 2) return True;
      if (e1->ips[1] != e2->ips[1]) return False;

      if (VG_(clo_backtrace_size) < 3) return True;
      if (e1->ips[2] != e2->ips[2]) return False;

      if (VG_(clo_backtrace_size) < 4) return True;
      if (e1->ips[3] != e2->ips[3]) return False;
      return True;

   case Vg_HighRes:
      vg_ec_cmpAlls++;
      /* Compare them all -- just do pointer comparison. */
      if (e1 != e2) return False;
      return True;

   default:
      VG_(core_panic)("VG_(eq_ExeContext): unrecognised VgRes");
   }
}


/* Take a snapshot of the client's stack, putting the up to 'n_ips' IPs 
   into 'ips'.  In order to be thread-safe, we pass in the thread's IP
   and FP.  Returns number of IPs put in 'ips'.  */
static UInt stack_snapshot2 ( Addr* ips, UInt n_ips, Addr ip, Addr fp,
                              Addr fp_min, Addr fp_max_orig )
{
   Int         i;
   Addr        fp_max;
   UInt        n_found = 0;

   VGP_PUSHCC(VgpExeContext);

   /* First snaffle IPs from the client's stack into ips[0 .. n_ips-1], 
      putting zeroes in when the trail goes cold, which we guess to be when
      FP is not a reasonable stack location.  We also assert that FP
      increases down the chain. */

   // Gives shorter stack trace for tests/badjump.c
   // JRS 2002-aug-16: I don't think this is a big deal; looks ok for
   // most "normal" backtraces.
   // NJN 2002-sep-05: traces for pthreaded programs are particularly bad.

   // JRS 2002-sep-17: hack, to round up fp_max to the end of the
   // current page, at least.  Dunno if it helps.
   // NJN 2002-sep-17: seems to -- stack traces look like 1.0.X again
   fp_max = (fp_max_orig + VKI_PAGE_SIZE - 1) & ~(VKI_PAGE_SIZE - 1);
   fp_max -= sizeof(Addr);

   /* Assertion broken before main() is reached in pthreaded programs;  the
    * offending stack traces only have one item.  --njn, 2002-aug-16 */
   /* vg_assert(fp_min <= fp_max);*/

   if (fp_min + 4000000 <= fp_max) {
      /* If the stack is ridiculously big, don't poke around ... but
         don't bomb out either.  Needed to make John Regehr's
         user-space threads package work. JRS 20021001 */
      ips[0] = ip;
      i = 1;
   } else {
      /* Get whatever we safely can ... */
      ips[0] = ip;
      fp = FIRST_STACK_FRAME(fp);
      for (i = 1; i < n_ips; i++) {
         if (!(fp_min <= fp && fp <= fp_max)) {
            //VG_(printf)("... out of range %p\n", fp);
            break; /* fp gone baaaad */
         }
         // NJN 2002-sep-17: monotonicity doesn't work -- gives wrong traces...
         //     if (fp >= ((UInt*)fp)[0]) {
         //   VG_(printf)("nonmonotonic\n");
         //    break; /* fp gone nonmonotonic */
         // }
         ips[i] = STACK_FRAME_RET(fp);  /* ret addr */
         fp     = STACK_FRAME_NEXT(fp);  /* old fp */
         //VG_(printf)("     %p\n", ips[i]);
      }
   }
   n_found = i;

   /* Put zeroes in the rest. */
   for (;  i < n_ips; i++) {
      ips[i] = 0;
   }
   VGP_POPCC(VgpExeContext);

   return n_found;
}

/* This guy is the head honcho here.  Take a snapshot of the client's
   stack.  Search our collection of ExeContexts to see if we already
   have it, and if not, allocate a new one.  Either way, return a
   pointer to the context.  If there is a matching context we
   guarantee to not allocate a new one.  Thus we never store
   duplicates, and so exact equality can be quickly done as equality
   on the returned ExeContext* values themselves.  Inspired by Hugs's
   Text type.  
*/
ExeContext* VG_(get_ExeContext2) ( Addr ip, Addr fp,
                                   Addr fp_min, Addr fp_max_orig )
{
   Int         i;
   Addr        ips[VG_DEEPEST_BACKTRACE];
   Bool        same;
   UWord       hash;
   ExeContext* new_ec;
   ExeContext* list;

   VGP_PUSHCC(VgpExeContext);

   init_ExeContext_storage();
   vg_assert(VG_(clo_backtrace_size) >= 1 
             && VG_(clo_backtrace_size) <= VG_DEEPEST_BACKTRACE);

   stack_snapshot2( ips, VG_(clo_backtrace_size),
                    ip, fp, fp_min, fp_max_orig );

   /* Now figure out if we've seen this one before.  First hash it so
      as to determine the list number. */

   hash = 0;
   for (i = 0; i < VG_(clo_backtrace_size); i++) {
      hash ^= ips[i];
      hash = (hash << 29) | (hash >> 3);
   }
   hash = hash % VG_N_EC_LISTS;

   /* And (the expensive bit) look a matching entry in the list. */

   vg_ec_searchreqs++;

   list = vg_ec_list[hash];

   while (True) {
      if (list == NULL) break;
      vg_ec_searchcmps++;
      same = True;
      for (i = 0; i < VG_(clo_backtrace_size); i++) {
         if (list->ips[i] != ips[i]) {
            same = False;
            break; 
         }
      }
      if (same) break;
      list = list->next;
   }

   if (list != NULL) {
      /* Yay!  We found it.  */
      VGP_POPCC(VgpExeContext);
      return list;
   }

   /* Bummer.  We have to allocate a new context record. */
   vg_ec_totstored++;

   new_ec = VG_(arena_malloc)( VG_AR_EXECTXT, 
                               sizeof(struct _ExeContext *) 
                               + VG_(clo_backtrace_size) * sizeof(Addr) );

   for (i = 0; i < VG_(clo_backtrace_size); i++)
      new_ec->ips[i] = ips[i];

   new_ec->next = vg_ec_list[hash];
   vg_ec_list[hash] = new_ec;

   VGP_POPCC(VgpExeContext);
   return new_ec;
}

void get_needed_regs(ThreadId tid, Addr* ip, Addr* fp, Addr* sp,
                     Addr* stack_highest_word)
{
   ThreadState* tst = & VG_(threads)[ tid ];
   *ip                 = INSTR_PTR(tst->arch);
   *fp                 = FRAME_PTR(tst->arch);
   *sp                 = STACK_PTR(tst->arch);
   *stack_highest_word = tst->stack_highest_word;

#ifdef __x86__
   /* Nasty little hack to deal with sysinfo syscalls - if libc is
      using the sysinfo page for syscalls (the TLS version does), then
      ip will always appear to be in that page when doing a syscall,
      not the actual libc function doing the syscall.  This check sees
      if IP is within the syscall code, and pops the return address
      off the stack so that ip is placed within the library function
      calling the syscall.  This makes stack backtraces much more
      useful.  */
   if (*ip >= VG_(client_trampoline_code)+VG_(tramp_syscall_offset) &&
       *ip < VG_(client_trampoline_code)+VG_(trampoline_code_length) &&
       VG_(is_addressable)(*sp, sizeof(Addr))) {
      *ip = *(Addr *)*sp;
      *sp += sizeof(Addr);
   }
#endif
}

ExeContext* VG_(get_ExeContext) ( ThreadId tid )
{
   Addr ip, fp, sp, stack_highest_word;

   get_needed_regs(tid, &ip, &fp, &sp, &stack_highest_word);
   return VG_(get_ExeContext2)(ip, fp, sp, stack_highest_word);
}

/* Take a snapshot of the client's stack, putting the up to 'n_ips' 
   instruction pointers into 'ips'.  In order to be thread-safe, we pass in
   the thread's IP and FP.  Returns number of IPs put in 'ips'.  */
UInt VG_(stack_snapshot) ( ThreadId tid, Addr* ips, UInt n_ips )
{
   Addr ip, fp, sp, stack_highest_word;

   get_needed_regs(tid, &ip, &fp, &sp, &stack_highest_word);
   return stack_snapshot2(ips, n_ips, ip, fp, sp, stack_highest_word);
}


Addr VG_(get_EIP_from_ExeContext) ( ExeContext* e, UInt n )
{
   if (n > VG_(clo_backtrace_size)) return 0;
   return e->ips[n];
}

Addr VG_(get_EIP) ( ThreadId tid )
{
   return INSTR_PTR(VG_(threads)[ tid ].arch);
}

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/

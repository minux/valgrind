
/*--------------------------------------------------------------------*/
/*--- C startup stuff, reached from vg_startup.S.                  ---*/
/*---                                                    vg_main.c ---*/
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

#include "vg_include.h"

#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---------------------------------------------------------------------
   Compute offsets into baseBlock.  See comments in vg_include.h.
   ------------------------------------------------------------------ */

/* The variables storing offsets. */

#define INVALID_OFFSET (-1)

Int VGOFF_(m_eax) = INVALID_OFFSET;
Int VGOFF_(m_ecx) = INVALID_OFFSET;
Int VGOFF_(m_edx) = INVALID_OFFSET;
Int VGOFF_(m_ebx) = INVALID_OFFSET;
Int VGOFF_(m_esp) = INVALID_OFFSET;
Int VGOFF_(m_ebp) = INVALID_OFFSET;
Int VGOFF_(m_esi) = INVALID_OFFSET;
Int VGOFF_(m_edi) = INVALID_OFFSET;
Int VGOFF_(m_eflags) = INVALID_OFFSET;
Int VGOFF_(m_dflag)  = INVALID_OFFSET;
Int VGOFF_(m_ssestate) = INVALID_OFFSET;
Int VGOFF_(ldt)   = INVALID_OFFSET;
Int VGOFF_(tls)   = INVALID_OFFSET;
Int VGOFF_(m_cs)  = INVALID_OFFSET;
Int VGOFF_(m_ss)  = INVALID_OFFSET;
Int VGOFF_(m_ds)  = INVALID_OFFSET;
Int VGOFF_(m_es)  = INVALID_OFFSET;
Int VGOFF_(m_fs)  = INVALID_OFFSET;
Int VGOFF_(m_gs)  = INVALID_OFFSET;
Int VGOFF_(m_eip) = INVALID_OFFSET;
Int VGOFF_(spillslots) = INVALID_OFFSET;
Int VGOFF_(sh_eax) = INVALID_OFFSET;
Int VGOFF_(sh_ecx) = INVALID_OFFSET;
Int VGOFF_(sh_edx) = INVALID_OFFSET;
Int VGOFF_(sh_ebx) = INVALID_OFFSET;
Int VGOFF_(sh_esp) = INVALID_OFFSET;
Int VGOFF_(sh_ebp) = INVALID_OFFSET;
Int VGOFF_(sh_esi) = INVALID_OFFSET;
Int VGOFF_(sh_edi) = INVALID_OFFSET;
Int VGOFF_(sh_eflags) = INVALID_OFFSET;

Int VGOFF_(helper_idiv_64_32) = INVALID_OFFSET;
Int VGOFF_(helper_div_64_32) = INVALID_OFFSET;
Int VGOFF_(helper_idiv_32_16) = INVALID_OFFSET;
Int VGOFF_(helper_div_32_16) = INVALID_OFFSET;
Int VGOFF_(helper_idiv_16_8) = INVALID_OFFSET;
Int VGOFF_(helper_div_16_8) = INVALID_OFFSET;
Int VGOFF_(helper_imul_32_64) = INVALID_OFFSET;
Int VGOFF_(helper_mul_32_64) = INVALID_OFFSET;
Int VGOFF_(helper_imul_16_32) = INVALID_OFFSET;
Int VGOFF_(helper_mul_16_32) = INVALID_OFFSET;
Int VGOFF_(helper_imul_8_16) = INVALID_OFFSET;
Int VGOFF_(helper_mul_8_16) = INVALID_OFFSET;
Int VGOFF_(helper_CLD) = INVALID_OFFSET;
Int VGOFF_(helper_STD) = INVALID_OFFSET;
Int VGOFF_(helper_get_dirflag) = INVALID_OFFSET;
Int VGOFF_(helper_CLC) = INVALID_OFFSET;
Int VGOFF_(helper_STC) = INVALID_OFFSET;
Int VGOFF_(helper_shldl) = INVALID_OFFSET;
Int VGOFF_(helper_shldw) = INVALID_OFFSET;
Int VGOFF_(helper_shrdl) = INVALID_OFFSET;
Int VGOFF_(helper_shrdw) = INVALID_OFFSET;
Int VGOFF_(helper_IN) = INVALID_OFFSET;
Int VGOFF_(helper_OUT) = INVALID_OFFSET;
Int VGOFF_(helper_RDTSC) = INVALID_OFFSET;
Int VGOFF_(helper_CPUID) = INVALID_OFFSET;
Int VGOFF_(helper_BSWAP) = INVALID_OFFSET;
Int VGOFF_(helper_bsf) = INVALID_OFFSET;
Int VGOFF_(helper_bsr) = INVALID_OFFSET;
Int VGOFF_(helper_fstsw_AX) = INVALID_OFFSET;
Int VGOFF_(helper_SAHF) = INVALID_OFFSET;
Int VGOFF_(helper_LAHF) = INVALID_OFFSET;
Int VGOFF_(helper_DAS) = INVALID_OFFSET;
Int VGOFF_(helper_DAA) = INVALID_OFFSET;
Int VGOFF_(helper_cmpxchg8b) = INVALID_OFFSET;
Int VGOFF_(helper_undefined_instruction) = INVALID_OFFSET;

/* MAX_NONCOMPACT_HELPERS can be increased easily.  If MAX_COMPACT_HELPERS is
 * increased too much, they won't really be compact any more... */
#define  MAX_COMPACT_HELPERS     8
#define  MAX_NONCOMPACT_HELPERS  50 

UInt VG_(n_compact_helpers)    = 0;
UInt VG_(n_noncompact_helpers) = 0;

Addr VG_(compact_helper_addrs)  [MAX_COMPACT_HELPERS];
Int  VG_(compact_helper_offsets)[MAX_COMPACT_HELPERS];
Addr VG_(noncompact_helper_addrs)  [MAX_NONCOMPACT_HELPERS];
Int  VG_(noncompact_helper_offsets)[MAX_NONCOMPACT_HELPERS];

/* This is the actual defn of baseblock. */
UInt VG_(baseBlock)[VG_BASEBLOCK_WORDS];

/* Client address space */
Addr VG_(client_base);	/* client address space limits */
Addr VG_(client_end);
Addr VG_(client_mapbase);
Addr VG_(client_trampoline_code);
Addr VG_(clstk_base);
Addr VG_(clstk_end);
Addr VG_(brk_base);	/* start of brk */
Addr VG_(brk_limit);	/* current brk */
Addr VG_(shadow_base);	/* skin's shadow memory */
Addr VG_(shadow_end);
Addr VG_(valgrind_base);	/* valgrind's address range */
Addr VG_(valgrind_mmap_end);	/* valgrind's mmaps are between valgrind_base and here */
Addr VG_(valgrind_end);

/* stage1 (main) executable */
Int  VG_(vgexecfd) = -1;

/* client executable */
Int  VG_(clexecfd) = -1;

/* Path to library directory */
const Char *VG_(libdir) = VG_LIBDIR;

/* our argc/argv */
Int  VG_(vg_argc);
Char **VG_(vg_argv);

/* PID of the main thread */
Int VG_(main_pid);

/* PGRP of process */
Int VG_(main_pgrp);

/* Maximum allowed application-visible file descriptor */
Int VG_(max_fd) = -1;

/* Words. */
static Int baB_off = 0;

/* jmp_buf for fatal signals */
Int	VG_(fatal_sigNo) = -1;
Bool	VG_(fatal_signal_set) = False;
jmp_buf VG_(fatal_signal_jmpbuf);

/* Returns the offset, in words. */
static Int alloc_BaB ( Int words )
{
   Int off = baB_off;
   baB_off += words;
   if (baB_off >= VG_BASEBLOCK_WORDS)
      VG_(core_panic)( "alloc_BaB: baseBlock is too small");

   return off;   
}

/* Align offset, in *bytes* */
static void align_BaB ( UInt align )
{
   vg_assert(2 == align || 4 == align || 8 == align || 16 == align);
   baB_off +=  (align-1);
   baB_off &= ~(align-1);
}

/* Allocate 1 word in baseBlock and set it to the given value. */
static Int alloc_BaB_1_set ( Addr a )
{
   Int off = alloc_BaB(1);
   VG_(baseBlock)[off] = (UInt)a;
   return off;
}

/* Registers a function in compact_helper_addrs;  compact_helper_offsets is
   filled in later. */
void VG_(register_compact_helper)(Addr a)
{
   if (MAX_COMPACT_HELPERS <= VG_(n_compact_helpers)) {
      VG_(printf)("Can only register %d compact helpers\n", 
                  MAX_COMPACT_HELPERS);
      VG_(core_panic)("Too many compact helpers registered");
   }
   VG_(compact_helper_addrs)[VG_(n_compact_helpers)] = a;
   VG_(n_compact_helpers)++;
}

/* Registers a function in noncompact_helper_addrs;  noncompact_helper_offsets
 * is filled in later.
 */
void VG_(register_noncompact_helper)(Addr a)
{
   if (MAX_NONCOMPACT_HELPERS <= VG_(n_noncompact_helpers)) {
      VG_(printf)("Can only register %d non-compact helpers\n", 
                  MAX_NONCOMPACT_HELPERS);
      VG_(printf)("Try increasing MAX_NON_COMPACT_HELPERS\n");
      VG_(core_panic)("Too many non-compact helpers registered");
   }
   VG_(noncompact_helper_addrs)[VG_(n_noncompact_helpers)] = a;
   VG_(n_noncompact_helpers)++;
}

/* Allocate offsets in baseBlock for the skin helpers */
static 
void assign_helpers_in_baseBlock(UInt n, Int offsets[], Addr addrs[])
{
   UInt i;
   for (i = 0; i < n; i++) 
      offsets[i] = alloc_BaB_1_set( addrs[i] );
}

Bool VG_(need_to_handle_esp_assignment)(void)
{
   return ( VG_(defined_new_mem_stack_4)()  ||
            VG_(defined_die_mem_stack_4)()  ||
            VG_(defined_new_mem_stack_8)()  ||
            VG_(defined_die_mem_stack_8)()  ||
            VG_(defined_new_mem_stack_12)() ||
            VG_(defined_die_mem_stack_12)() ||
            VG_(defined_new_mem_stack_16)() ||
            VG_(defined_die_mem_stack_16)() ||
            VG_(defined_new_mem_stack_32)() ||
            VG_(defined_die_mem_stack_32)() ||
            VG_(defined_new_mem_stack)()    ||
            VG_(defined_die_mem_stack)()
          );
}

/* Here we assign actual offsets.  It's important to get the most
   popular referents within 128 bytes of the start, so we can take
   advantage of short addressing modes relative to %ebp.  Popularity
   of offsets was measured on 22 Feb 02 running a KDE application, and
   the slots rearranged accordingly, with a 1.5% reduction in total
   size of translations. */
static void vg_init_baseBlock ( void )
{
   /* Those with offsets under 128 are carefully chosen. */

   /* WORD offsets in this column */
   /* 0   */ VGOFF_(m_eax)     = alloc_BaB(1);
   /* 1   */ VGOFF_(m_ecx)     = alloc_BaB(1);
   /* 2   */ VGOFF_(m_edx)     = alloc_BaB(1);
   /* 3   */ VGOFF_(m_ebx)     = alloc_BaB(1);
   /* 4   */ VGOFF_(m_esp)     = alloc_BaB(1);
   /* 5   */ VGOFF_(m_ebp)     = alloc_BaB(1);
   /* 6   */ VGOFF_(m_esi)     = alloc_BaB(1);
   /* 7   */ VGOFF_(m_edi)     = alloc_BaB(1);
   /* 8   */ VGOFF_(m_eflags)  = alloc_BaB(1);

   if (VG_(needs).shadow_regs) {
      /* 9   */ VGOFF_(sh_eax)    = alloc_BaB(1);
      /* 10  */ VGOFF_(sh_ecx)    = alloc_BaB(1);
      /* 11  */ VGOFF_(sh_edx)    = alloc_BaB(1);
      /* 12  */ VGOFF_(sh_ebx)    = alloc_BaB(1);
      /* 13  */ VGOFF_(sh_esp)    = alloc_BaB(1);
      /* 14  */ VGOFF_(sh_ebp)    = alloc_BaB(1);
      /* 15  */ VGOFF_(sh_esi)    = alloc_BaB(1);
      /* 16  */ VGOFF_(sh_edi)    = alloc_BaB(1);
      /* 17  */ VGOFF_(sh_eflags) = alloc_BaB(1);
   }

   /* 9,10,11 or 18,19,20... depends on number whether shadow regs are used
    * and on compact helpers registered */ 

   /* Make these most-frequently-called specialised ones compact, if they
      are used. */
   if (VG_(defined_new_mem_stack_4)())
      VG_(register_compact_helper)( (Addr) VG_(tool_interface).track_new_mem_stack_4);

   if (VG_(defined_die_mem_stack_4)())
      VG_(register_compact_helper)( (Addr) VG_(tool_interface).track_die_mem_stack_4);

   /* (9 or 18) + n_compact_helpers  */
   /* Allocate slots for compact helpers */
   assign_helpers_in_baseBlock(VG_(n_compact_helpers), 
                               VG_(compact_helper_offsets), 
                               VG_(compact_helper_addrs));

   /* (9/10 or 18/19) + n_compact_helpers */
   VGOFF_(m_eip) = alloc_BaB(1);

   /* There are currently 24 spill slots */
   /* (11+/20+ .. 32+/43+) + n_compact_helpers.  This can overlap the magic
    * boundary at >= 32 words, but most spills are to low numbered spill
    * slots, so the ones above the boundary don't see much action. */
   VGOFF_(spillslots) = alloc_BaB(VG_MAX_SPILLSLOTS);

   /* I gave up counting at this point.  Since they're above the
      short-amode-boundary, there's no point. */

   VGOFF_(m_dflag) = alloc_BaB(1);

   /* The FPU/SSE state.  This _must_ be 16-byte aligned. */
   align_BaB(16);
   VGOFF_(m_ssestate) = alloc_BaB(VG_SIZE_OF_SSESTATE_W);
   vg_assert( 
      (  ((UInt)(& VG_(baseBlock)[VGOFF_(m_ssestate)]))
         % 16  )
      == 0
   );

   /* This thread's LDT and TLS pointers, and segment registers. */
   VGOFF_(ldt)   = alloc_BaB(1);
   VGOFF_(tls)   = alloc_BaB(1);
   VGOFF_(m_cs)  = alloc_BaB(1);
   VGOFF_(m_ss)  = alloc_BaB(1);
   VGOFF_(m_ds)  = alloc_BaB(1);
   VGOFF_(m_es)  = alloc_BaB(1);
   VGOFF_(m_fs)  = alloc_BaB(1);
   VGOFF_(m_gs)  = alloc_BaB(1);

   VG_(register_noncompact_helper)( (Addr) & VG_(do_useseg) );

#define REG(kind, size) \
   if (VG_(defined_##kind##_mem_stack##size)()) \
      VG_(register_noncompact_helper)(           \
          (Addr) VG_(tool_interface).track_##kind##_mem_stack##size );

   REG(new, _8);
   REG(new, _12);
   REG(new, _16);
   REG(new, _32);
   REG(new, );
   REG(die, _8);
   REG(die, _12);
   REG(die, _16);
   REG(die, _32);
   REG(die, );
#undef REG

   if (VG_(need_to_handle_esp_assignment)())
      VG_(register_noncompact_helper)((Addr) VG_(unknown_esp_update));

   /* Helper functions. */
   VGOFF_(helper_idiv_64_32)
      = alloc_BaB_1_set( (Addr) & VG_(helper_idiv_64_32));
   VGOFF_(helper_div_64_32)
      = alloc_BaB_1_set( (Addr) & VG_(helper_div_64_32));
   VGOFF_(helper_idiv_32_16)
      = alloc_BaB_1_set( (Addr) & VG_(helper_idiv_32_16));
   VGOFF_(helper_div_32_16)
      = alloc_BaB_1_set( (Addr) & VG_(helper_div_32_16));
   VGOFF_(helper_idiv_16_8)
      = alloc_BaB_1_set( (Addr) & VG_(helper_idiv_16_8));
   VGOFF_(helper_div_16_8)
      = alloc_BaB_1_set( (Addr) & VG_(helper_div_16_8));

   VGOFF_(helper_imul_32_64)
      = alloc_BaB_1_set( (Addr) & VG_(helper_imul_32_64));
   VGOFF_(helper_mul_32_64)
      = alloc_BaB_1_set( (Addr) & VG_(helper_mul_32_64));
   VGOFF_(helper_imul_16_32)
      = alloc_BaB_1_set( (Addr) & VG_(helper_imul_16_32));
   VGOFF_(helper_mul_16_32)
      = alloc_BaB_1_set( (Addr) & VG_(helper_mul_16_32));
   VGOFF_(helper_imul_8_16)
      = alloc_BaB_1_set( (Addr) & VG_(helper_imul_8_16));
   VGOFF_(helper_mul_8_16)
      = alloc_BaB_1_set( (Addr) & VG_(helper_mul_8_16));

   VGOFF_(helper_CLD)
      = alloc_BaB_1_set( (Addr) & VG_(helper_CLD));
   VGOFF_(helper_STD)
      = alloc_BaB_1_set( (Addr) & VG_(helper_STD));
   VGOFF_(helper_get_dirflag)
      = alloc_BaB_1_set( (Addr) & VG_(helper_get_dirflag));

   VGOFF_(helper_CLC)
      = alloc_BaB_1_set( (Addr) & VG_(helper_CLC));
   VGOFF_(helper_STC)
      = alloc_BaB_1_set( (Addr) & VG_(helper_STC));

   VGOFF_(helper_shldl)
      = alloc_BaB_1_set( (Addr) & VG_(helper_shldl));
   VGOFF_(helper_shldw)
      = alloc_BaB_1_set( (Addr) & VG_(helper_shldw));
   VGOFF_(helper_shrdl)
      = alloc_BaB_1_set( (Addr) & VG_(helper_shrdl));
   VGOFF_(helper_shrdw)
      = alloc_BaB_1_set( (Addr) & VG_(helper_shrdw));

   VGOFF_(helper_RDTSC)
      = alloc_BaB_1_set( (Addr) & VG_(helper_RDTSC));
   VGOFF_(helper_CPUID)
      = alloc_BaB_1_set( (Addr) & VG_(helper_CPUID));

   VGOFF_(helper_bsf)
      = alloc_BaB_1_set( (Addr) & VG_(helper_bsf));
   VGOFF_(helper_bsr)
      = alloc_BaB_1_set( (Addr) & VG_(helper_bsr));

   VGOFF_(helper_fstsw_AX)
      = alloc_BaB_1_set( (Addr) & VG_(helper_fstsw_AX));
   VGOFF_(helper_SAHF)
      = alloc_BaB_1_set( (Addr) & VG_(helper_SAHF));
   VGOFF_(helper_LAHF)
      = alloc_BaB_1_set( (Addr) & VG_(helper_LAHF));
   VGOFF_(helper_DAS)
      = alloc_BaB_1_set( (Addr) & VG_(helper_DAS));
   VGOFF_(helper_DAA)
      = alloc_BaB_1_set( (Addr) & VG_(helper_DAA));
   VGOFF_(helper_IN)
      = alloc_BaB_1_set( (Addr) & VG_(helper_IN));
   VGOFF_(helper_OUT)
      = alloc_BaB_1_set( (Addr) & VG_(helper_OUT));
   VGOFF_(helper_cmpxchg8b)
      = alloc_BaB_1_set( (Addr) & VG_(helper_cmpxchg8b));

   VGOFF_(helper_undefined_instruction)
      = alloc_BaB_1_set( (Addr) & VG_(helper_undefined_instruction));

   /* Allocate slots for noncompact helpers */
   assign_helpers_in_baseBlock(VG_(n_noncompact_helpers), 
                               VG_(noncompact_helper_offsets), 
                               VG_(noncompact_helper_addrs));


   /* Initialise slots that require it */
   VG_(copy_m_state_static_to_baseBlock)();

   /* Pretend the root thread has a completely empty LDT to start with. */
   VG_(baseBlock)[VGOFF_(ldt)] = (UInt)NULL;

   /* Pretend the root thread has no TLS array for now. */
   VG_(baseBlock)[VGOFF_(tls)] = (UInt)NULL;

   /* Initialise shadow regs */
   if (VG_(needs).shadow_regs) {
      VG_(baseBlock)[VGOFF_(sh_esp)]    = 
      VG_(baseBlock)[VGOFF_(sh_ebp)]    =
      VG_(baseBlock)[VGOFF_(sh_eax)]    =
      VG_(baseBlock)[VGOFF_(sh_ecx)]    =
      VG_(baseBlock)[VGOFF_(sh_edx)]    =
      VG_(baseBlock)[VGOFF_(sh_ebx)]    =
      VG_(baseBlock)[VGOFF_(sh_esi)]    =
      VG_(baseBlock)[VGOFF_(sh_edi)]    = 0;
      VG_(baseBlock)[VGOFF_(sh_eflags)] = 0;
      VG_TRACK( post_regs_write_init );
   }
}


/* ---------------------------------------------------------------------
   Global entities which are not referenced from generated code.
   ------------------------------------------------------------------ */

/* Ditto our signal delivery stack. */
UInt VG_(sigstack)[VG_SIGSTACK_SIZE_W];

/* Saving stuff across system calls. */
__attribute__ ((aligned (16)))
UInt VG_(real_sse_state_saved_over_syscall)[VG_SIZE_OF_SSESTATE_W];
Addr VG_(esp_saved_over_syscall);

/* Counts downwards in vg_run_innerloop. */
UInt VG_(dispatch_ctr);


/* 64-bit counter for the number of basic blocks done. */
ULong VG_(bbs_done);
/* 64-bit counter for the number of bbs to go before a debug exit. */
ULong VG_(bbs_to_go);

/* This is the ThreadId of the last thread the scheduler ran. */
ThreadId VG_(last_run_tid) = 0;

/* This is the argument to __NR_exit() supplied by the first thread to
   call that syscall.  We eventually pass that to __NR_exit() for
   real. */
Int VG_(exitcode) = 0;

/* Tell the logging mechanism whether we are logging to a file
   descriptor or a socket descriptor. */
Bool VG_(logging_to_filedes) = True;

/* Is this a SSE/SSE2-capable CPU?  If so, we had better save/restore
   the SSE state all over the place.  This is set up very early, in
   vg_startup.S.  We have to determine it early since we can't even
   correctly snapshot the startup machine state without it. */
/* Initially True.  Safer to err on the side of SSEness and get SIGILL
   than to not notice for some reason that we have SSE and get weird
   errors later on. */
Bool VG_(have_ssestate) = True;


/* ---------------------------------------------------------------------
   Counters, for informational purposes only.
   ------------------------------------------------------------------ */

/* Number of lookups which miss the fast tt helper. */
UInt VG_(tt_fast_misses) = 0;


/* Counts for TT/TC informational messages. */

/* Number and total o/t size of translations overall. */
UInt VG_(overall_in_count) = 0;
UInt VG_(overall_in_osize) = 0;
UInt VG_(overall_in_tsize) = 0;
/* Number and total o/t size of discards overall. */
UInt VG_(overall_out_count) = 0;
UInt VG_(overall_out_osize) = 0;
UInt VG_(overall_out_tsize) = 0;
/* The number of discards of TT/TC. */
UInt VG_(number_of_tc_discards) = 0;
/* Counts of chain and unchain operations done. */
UInt VG_(bb_enchain_count) = 0;
UInt VG_(bb_dechain_count) = 0;
/* Number of unchained jumps performed. */
UInt VG_(unchained_jumps_done) = 0;


/* Counts pertaining to the register allocator. */

/* total number of uinstrs input to reg-alloc */
UInt VG_(uinstrs_prealloc) = 0;

/* total number of uinstrs added due to spill code */
UInt VG_(uinstrs_spill) = 0;

/* number of bbs requiring spill code */
UInt VG_(translations_needing_spill) = 0;

/* total of register ranks over all translations */
UInt VG_(total_reg_rank) = 0;


/* Counts pertaining to internal sanity checking. */
UInt VG_(sanity_fast_count) = 0;
UInt VG_(sanity_slow_count) = 0;

/* Counts pertaining to the scheduler. */
UInt VG_(num_scheduling_events_MINOR) = 0;
UInt VG_(num_scheduling_events_MAJOR) = 0;


/* ---------------------------------------------------------------------
   Values derived from command-line options.
   ------------------------------------------------------------------ */

/* Define, and set defaults. */
Bool   VG_(clo_error_limit)    = True;
Bool   VG_(clo_GDB_attach)     = False;
Char*  VG_(clo_GDB_path)       = GDB_PATH;
Bool   VG_(clo_gen_suppressions) = False;
Int    VG_(sanity_level)       = 1;
Int    VG_(clo_verbosity)      = 1;
Bool   VG_(clo_demangle)       = True;
Bool   VG_(clo_trace_children) = False;

/* See big comment in vg_include.h for meaning of these three.
   fd is initially stdout, for --help, but gets moved to stderr by default
   immediately afterwards. */
VgLogTo VG_(clo_log_to)        = VgLogTo_Fd;
Int     VG_(clo_logfile_fd)    = 1;
Char*   VG_(clo_logfile_name)  = NULL;

Int    VG_(clo_input_fd)       = 0; /* stdin */
Int    VG_(clo_n_suppressions) = 0;
Char*  VG_(clo_suppressions)[VG_CLO_MAX_SFILES];
Bool   VG_(clo_profile)        = False;
Bool   VG_(clo_single_step)    = False;
Bool   VG_(clo_optimise)       = True;
UChar  VG_(clo_trace_codegen)  = 0; // 00000000b
Bool   VG_(clo_trace_syscalls) = False;
Bool   VG_(clo_trace_signals)  = False;
Bool   VG_(clo_trace_symtab)   = False;
Bool   VG_(clo_trace_sched)    = False;
Int    VG_(clo_trace_pthread_level) = 0;
ULong  VG_(clo_stop_after)     = 1000000000000000LL;
Int    VG_(clo_dump_error)     = 0;
Int    VG_(clo_backtrace_size) = 4;
Char*  VG_(clo_weird_hacks)    = NULL;
Bool   VG_(clo_run_libc_freeres) = True;
Bool   VG_(clo_track_fds)      = False;
Bool   VG_(clo_chain_bb)       = True;
Bool   VG_(clo_show_below_main) = False;
Bool   VG_(clo_pointercheck)   = True;
Bool   VG_(clo_branchpred)     = False;

static Bool   VG_(clo_wait_for_gdb)   = False;

/* If we're doing signal routing, poll for signals every 50mS by
   default. */
Int    VG_(clo_signal_polltime) = 50;

/* These flags reduce thread wakeup latency on syscall completion and
   signal delivery, respectively.  The downside is possible unfairness. */
Bool   VG_(clo_lowlat_syscalls) = False; /* low-latency syscalls */
Bool   VG_(clo_lowlat_signals)  = False; /* low-latency signals */

/* This Bool is needed by wrappers in vg_clientmalloc.c to decide how
   to behave.  Initially we say False. */
Bool VG_(running_on_simd_CPU) = False;

/* Holds client's %esp at the point we gained control. */
Addr VG_(esp_at_startup);

/* Indicates presence, and holds address of client's sysinfo page, a
   feature of some modern kernels used to provide vsyscalls, etc. */
Bool VG_(sysinfo_page_exists) = False;
Addr VG_(sysinfo_page_addr) = 0;

/* As deduced from VG_(esp_at_startup), the client's argc, argv[] and
   envp[] as extracted from the client's stack at startup-time. */
Int    VG_(client_argc);
Char** VG_(client_argv);
Char** VG_(client_envp);

/* ---------------------------------------------------------------------
   Processing of command-line options.
   ------------------------------------------------------------------ */

void VG_(bad_option) ( Char* opt )
{
   VG_(shutdown_logging)();
   VG_(clo_log_to)     = VgLogTo_Fd;
   VG_(clo_logfile_fd) = 2; /* stderr */
   VG_(printf)("valgrind.so: Bad option `%s'; aborting.\n", opt);
   VG_(exit)(1);
}

static void config_error ( Char* msg )
{
   VG_(shutdown_logging)();
   VG_(clo_log_to)     = VgLogTo_Fd;
   VG_(clo_logfile_fd) = 2; /* stderr */
   VG_(printf)(
      "valgrind: Startup or configuration error:\n   %s\n", msg);
   VG_(printf)(
      "valgrind: Unable to start up properly.  Giving up.\n");
   VG_(exit)(1);
}

void VG_(usage) ( void )
{
   Char* usage1 = 
"usage: valgrind [options] prog-and-args\n"
"\n"
"  common user options for all Valgrind tools, with defaults in [ ]:\n"
"    --tool=<name>             Use the Valgrind tool named <name> [memcheck]\n"
"    --help                    show this message\n"
"    --version                 show version\n"
"    -q --quiet                run silently; only print error msgs\n"
"    -v --verbose              be more verbose, incl counts of errors\n"
"    --trace-children=no|yes   Valgrind-ise child processes? [no]\n"
"    --track-fds=no|yes        Track open file descriptors? [no]\n"
"\n"
"  uncommon user options for all Valgrind tools:\n"
"    --run-libc-freeres=no|yes Free up glibc memory at exit? [yes]\n"
"    --weird-hacks=hack1,hack2,...  [none]\n"
"         recognised hacks are: ioctl-VTIME truncate-writes lax-ioctls\n"
"    --signal-polltime=<time>  time, in mS, we should poll for signals.\n"
"                              Only applies for older kernels which need\n"
"                              signal routing [50]\n"
"    --lowlat-signals=no|yes   improve wake-up latency when a thread receives\n"
"			       a signal [no]\n"
"    --lowlat-syscalls=no|yes  improve wake-up latency when a thread's\n"
"			       syscall completes [no]\n"
"    --pointercheck=no|yes     enforce client address space limits [yes]\n"
"\n"
"  user options for Valgrind tools that report errors:\n"
"    --logfile-fd=<number>     file descriptor for messages [2=stderr]\n"
"    --logfile=<file>          log messages to <file>.pid<pid>\n"
"    --logsocket=ipaddr:port   log messages to socket ipaddr:port\n"
"    --demangle=no|yes         automatically demangle C++ names? [yes]\n"
"    --num-callers=<number>    show <num> callers in stack traces [4]\n"
"    --error-limit=no|yes      stop showing new errors if too many? [yes]\n"
"    --show-below-main=no|yes  continue stack traces below main() [no]\n"
"    --suppressions=<filename> suppress errors described in <filename>\n"
"    --gen-suppressions=no|yes print suppressions for errors detected [no]\n"

"    --gdb-attach=no|yes       start GDB when errors detected? [no]\n"
"    --gdb-path=/path/to/gdb   path to the GDB to use [/usr/bin/gdb]\n"
"    --input-fd=<number>       file descriptor for (gdb) input [0=stdin]\n"
"\n";

   Char* usage2 = 
"\n"
"  debugging options for all Valgrind tools:\n"
"    --sanity-level=<number>   level of sanity checking to do [1]\n"
"    --single-step=no|yes      translate each instr separately? [no]\n"
"    --optimise=no|yes         improve intermediate code? [yes]\n"
"    --profile=no|yes          profile? (tool must be built for it) [no]\n"
"    --chain-bb=no|yes         do basic-block chaining? [yes]\n"
"    --branchpred=yes|no       generate branch prediction hints [no]\n"
"    --trace-codegen=<XXXXX>   show generated code? (X = 0|1) [00000]\n"
"    --trace-syscalls=no|yes   show all system calls? [no]\n"
"    --trace-signals=no|yes    show signal handling details? [no]\n"
"    --trace-symtab=no|yes     show symbol table details? [no]\n"
"    --trace-sched=no|yes      show thread scheduler details? [no]\n"
"    --trace-pthread=none|some|all  show pthread event details? [none]\n"
"    --stop-after=<number>     switch to real CPU after executing\n"
"                              <number> basic blocks [infinity]\n"
"    --wait-for-gdb=yes|no     pause on startup to wait for gdb attach\n"
"\n"
"  debugging options for Valgrind tools that report errors\n"
"    --dump-error=<number>     show translation for basic block associated\n"
"                              with <number>'th error context [0=show none]\n"
"\n";

   Char* usage3 =
"\n"
"  Extra options are read from env variable $VALGRIND_OPTS\n"
"\n"
"  Valgrind is Copyright (C) 2000-2004 Julian Seward\n"
"  and licensed under the GNU General Public License, version 2.\n"
"  Bug reports, feedback, admiration, abuse, etc, to: %s.\n"
"\n"
"  Tools are copyright and licensed by their authors.  See each\n"
"  tool's start-up message for more information.\n"
"\n";

   VG_(printf)(usage1);
   if (VG_(details).name) {
      VG_(printf)("  user options for %s:\n", VG_(details).name);
      /* Don't print skin string directly for security, ha! */
      if (VG_(needs).command_line_options)
	 SK_(print_usage)();
      else
	 VG_(printf)("    (none)\n");
   }
   VG_(printf)(usage2);

   if (VG_(details).name) {
      VG_(printf)("  debugging options for %s:\n", VG_(details).name);
   
      if (VG_(needs).command_line_options)
	 SK_(print_debug_usage)();
      else
	 VG_(printf)("    (none)\n");
   }
   VG_(printf)(usage3, VG_BUGS_TO);

   VG_(shutdown_logging)();
   VG_(clo_log_to)     = VgLogTo_Fd;
   VG_(clo_logfile_fd) = 2; /* stderr */
   VG_(exit)(1);
}


static void process_cmd_line_options ( const KickstartParams *kp )
{
   Int argc;
   Char **argv;
   Int   i, eventually_logfile_fd;
   Int	*auxp;

#  define ISSPACE(cc)      ((cc) == ' ' || (cc) == '\t' || (cc) == '\n')

   /* log to stderr by default, but usage message goes to stdout */
   eventually_logfile_fd = 2; 

   /* Once logging is started, we can safely send messages pertaining
      to failures in initialisation. */
   VG_(startup_logging)();

   /* Check for sane path in ./configure --prefix=... */
   if (VG_LIBDIR[0] != '/') 
     config_error("Please use absolute paths in "
                  "./configure --prefix=... or --libdir=...");

   for(auxp = kp->client_auxv; auxp[0] != VKI_AT_NULL; auxp += 2) {
      switch(auxp[0]) {
      case VKI_AT_SYSINFO:
	 VG_(sysinfo_page_exists) = True;
	 auxp[1] = (Int)(VG_(client_trampoline_code) + VG_(tramp_syscall_offset));
	 VG_(sysinfo_page_addr) = auxp[1];
	 break;
      }
   } 

   VG_(client_envp) = kp->client_envp;

   argc = kp->argc;
   argv = kp->argv;

   VG_(vg_argc) = argc;
   VG_(vg_argv) = argv;

   /* We know the initial ESP is pointing at argc/argv */
   VG_(client_argc) = *(Int *)kp->client_esp;
   VG_(client_argv) = (Char **)(kp->client_esp + sizeof(Int));

   for (i = 1; i < argc; i++) {
      /* Ignore these options - they've already been handled */
      if (VG_CLO_STREQN(7, argv[i], "--tool=") ||
	  VG_CLO_STREQN(7, argv[i], "--skin="))
	 continue;
      if (VG_CLO_STREQN(7, argv[i], "--exec="))
	 continue;

      if (     VG_CLO_STREQ(argv[i], "--"))
	 continue;
      else if (VG_CLO_STREQ(argv[i], "-v") ||
               VG_CLO_STREQ(argv[i], "--verbose"))
         VG_(clo_verbosity)++;
      else if (VG_CLO_STREQ(argv[i], "-q") ||
               VG_CLO_STREQ(argv[i], "--quiet"))
         VG_(clo_verbosity)--;

      else if (VG_CLO_STREQ(argv[i], "--error-limit=yes"))
         VG_(clo_error_limit) = True;
      else if (VG_CLO_STREQ(argv[i], "--error-limit=no"))
         VG_(clo_error_limit) = False;

      else if (VG_CLO_STREQ(argv[i], "--gdb-attach=yes"))
         VG_(clo_GDB_attach) = True;
      else if (VG_CLO_STREQ(argv[i], "--gdb-attach=no"))
         VG_(clo_GDB_attach) = False;

      else if (VG_CLO_STREQN(11,argv[i], "--gdb-path="))
         VG_(clo_GDB_path) = &argv[i][11];

      else if (VG_CLO_STREQ(argv[i], "--gen-suppressions=yes"))
         VG_(clo_gen_suppressions) = True;
      else if (VG_CLO_STREQ(argv[i], "--gen-suppressions=no"))
         VG_(clo_gen_suppressions) = False;

      else if (VG_CLO_STREQ(argv[i], "--show-below-main=yes"))
         VG_(clo_show_below_main) = True;
      else if (VG_CLO_STREQ(argv[i], "--show-below-main=no"))
         VG_(clo_show_below_main) = False;

      else if (VG_CLO_STREQ(argv[i], "--pointercheck=yes"))
         VG_(clo_pointercheck) = True;
      else if (VG_CLO_STREQ(argv[i], "--pointercheck=no"))
         VG_(clo_pointercheck) = False;

      else if (VG_CLO_STREQ(argv[i], "--demangle=yes"))
         VG_(clo_demangle) = True;
      else if (VG_CLO_STREQ(argv[i], "--demangle=no"))
         VG_(clo_demangle) = False;

      else if (VG_CLO_STREQ(argv[i], "--trace-children=yes"))
         VG_(clo_trace_children) = True;
      else if (VG_CLO_STREQ(argv[i], "--trace-children=no"))
         VG_(clo_trace_children) = False;

      else if (VG_CLO_STREQ(argv[i], "--run-libc-freeres=yes"))
         VG_(clo_run_libc_freeres) = True;
      else if (VG_CLO_STREQ(argv[i], "--run-libc-freeres=no"))
         VG_(clo_run_libc_freeres) = False;

      else if (VG_CLO_STREQ(argv[i], "--track-fds=yes"))
         VG_(clo_track_fds) = True;
      else if (VG_CLO_STREQ(argv[i], "--track-fds=no"))
         VG_(clo_track_fds) = False;

      else if (VG_CLO_STREQN(15, argv[i], "--sanity-level="))
         VG_(sanity_level) = (Int)VG_(atoll)(&argv[i][15]);

      else if (VG_CLO_STREQN(13, argv[i], "--logfile-fd=")) {
         VG_(clo_log_to)       = VgLogTo_Fd;
         VG_(clo_logfile_name) = NULL;
         eventually_logfile_fd = (Int)VG_(atoll)(&argv[i][13]);
      }

      else if (VG_CLO_STREQN(10, argv[i], "--logfile=")) {
         VG_(clo_log_to)       = VgLogTo_File;
         VG_(clo_logfile_name) = &argv[i][10];
      }

      else if (VG_CLO_STREQN(12, argv[i], "--logsocket=")) {
         VG_(clo_log_to)       = VgLogTo_Socket;
         VG_(clo_logfile_name) = &argv[i][12];
      }

      else if (VG_CLO_STREQN(11, argv[i], "--input-fd="))
         VG_(clo_input_fd)     = (Int)VG_(atoll)(&argv[i][11]);

      else if (VG_CLO_STREQN(15, argv[i], "--suppressions=")) {
         if (VG_(clo_n_suppressions) >= VG_CLO_MAX_SFILES) {
            VG_(message)(Vg_UserMsg, "Too many suppression files specified.");
            VG_(message)(Vg_UserMsg, 
                         "Increase VG_CLO_MAX_SFILES and recompile.");
            VG_(bad_option)(argv[i]);
         }
         VG_(clo_suppressions)[VG_(clo_n_suppressions)] = &argv[i][15];
         VG_(clo_n_suppressions)++;
      }
      else if (VG_CLO_STREQ(argv[i], "--profile=yes"))
         VG_(clo_profile) = True;
      else if (VG_CLO_STREQ(argv[i], "--profile=no"))
         VG_(clo_profile) = False;

      else if (VG_CLO_STREQ(argv[i], "--chain-bb=yes"))
	 VG_(clo_chain_bb) = True;
      else if (VG_CLO_STREQ(argv[i], "--chain-bb=no"))
	 VG_(clo_chain_bb) = False;

      else if (VG_CLO_STREQ(argv[i], "--branchpred=yes"))
	 VG_(clo_branchpred) = True;
      else if (VG_CLO_STREQ(argv[i], "--branchpred=no"))
	 VG_(clo_branchpred) = False;

      else if (VG_CLO_STREQ(argv[i], "--single-step=yes"))
         VG_(clo_single_step) = True;
      else if (VG_CLO_STREQ(argv[i], "--single-step=no"))
         VG_(clo_single_step) = False;

      else if (VG_CLO_STREQ(argv[i], "--optimise=yes"))
         VG_(clo_optimise) = True;
      else if (VG_CLO_STREQ(argv[i], "--optimise=no"))
         VG_(clo_optimise) = False;

      /* "vwxyz" --> 000zyxwv (binary) */
      else if (VG_CLO_STREQN(16, argv[i], "--trace-codegen=")) {
         Int j;
         char* opt = & argv[i][16];
   
         if (5 != VG_(strlen)(opt)) {
            VG_(message)(Vg_UserMsg, 
                         "--trace-codegen argument must have 5 digits");
            VG_(bad_option)(argv[i]);
         }
         for (j = 0; j < 5; j++) {
            if      ('0' == opt[j]) { /* do nothing */ }
            else if ('1' == opt[j]) VG_(clo_trace_codegen) |= (1 << j);
            else {
               VG_(message)(Vg_UserMsg, "--trace-codegen argument can only "
                                        "contain 0s and 1s");
               VG_(bad_option)(argv[i]);
            }
         }
      }

      else if (VG_CLO_STREQ(argv[i], "--trace-syscalls=yes"))
         VG_(clo_trace_syscalls) = True;
      else if (VG_CLO_STREQ(argv[i], "--trace-syscalls=no"))
         VG_(clo_trace_syscalls) = False;

      else if (VG_CLO_STREQ(argv[i], "--trace-signals=yes"))
         VG_(clo_trace_signals) = True;
      else if (VG_CLO_STREQ(argv[i], "--trace-signals=no"))
         VG_(clo_trace_signals) = False;

      else if (VG_CLO_STREQ(argv[i], "--trace-symtab=yes"))
         VG_(clo_trace_symtab) = True;
      else if (VG_CLO_STREQ(argv[i], "--trace-symtab=no"))
         VG_(clo_trace_symtab) = False;

      else if (VG_CLO_STREQ(argv[i], "--trace-sched=yes"))
         VG_(clo_trace_sched) = True;
      else if (VG_CLO_STREQ(argv[i], "--trace-sched=no"))
         VG_(clo_trace_sched) = False;

      else if (VG_CLO_STREQ(argv[i], "--trace-pthread=none"))
         VG_(clo_trace_pthread_level) = 0;
      else if (VG_CLO_STREQ(argv[i], "--trace-pthread=some"))
         VG_(clo_trace_pthread_level) = 1;
      else if (VG_CLO_STREQ(argv[i], "--trace-pthread=all"))
         VG_(clo_trace_pthread_level) = 2;

      else if (VG_CLO_STREQN(14, argv[i], "--weird-hacks="))
         VG_(clo_weird_hacks) = &argv[i][14];

      else if (VG_CLO_STREQN(17, argv[i], "--signal-polltime="))
	 VG_(clo_signal_polltime) = VG_(atoll)(&argv[i][17]);

      else if (VG_CLO_STREQ(argv[i], "--lowlat-signals=yes"))
	 VG_(clo_lowlat_signals) = True;
      else if (VG_CLO_STREQ(argv[i], "--lowlat-signals=no"))
	 VG_(clo_lowlat_signals) = False;

      else if (VG_CLO_STREQ(argv[i], "--lowlat-syscalls=yes"))
	 VG_(clo_lowlat_syscalls) = True;
      else if (VG_CLO_STREQ(argv[i], "--lowlat-syscalls=no"))
	 VG_(clo_lowlat_syscalls) = False;

      else if (VG_CLO_STREQN(13, argv[i], "--stop-after="))
         VG_(clo_stop_after) = VG_(atoll)(&argv[i][13]);

      else if (VG_CLO_STREQN(13, argv[i], "--dump-error="))
         VG_(clo_dump_error) = (Int)VG_(atoll)(&argv[i][13]);

      else if (VG_CLO_STREQ(argv[i], "--wait-for-gdb=yes"))
	 VG_(clo_wait_for_gdb) = True;
      else if (VG_CLO_STREQ(argv[i], "--wait-for-gdb=no"))
	 VG_(clo_wait_for_gdb) = False;

      else if (VG_CLO_STREQN(14, argv[i], "--num-callers=")) {
         /* Make sure it's sane. */
	 VG_(clo_backtrace_size) = (Int)VG_(atoll)(&argv[i][14]);
         if (VG_(clo_backtrace_size) < 1)
            VG_(clo_backtrace_size) = 1;
         if (VG_(clo_backtrace_size) >= VG_DEEPEST_BACKTRACE)
            VG_(clo_backtrace_size) = VG_DEEPEST_BACKTRACE;
      }

      else if (VG_(needs).command_line_options) {
         Bool ok = SK_(process_cmd_line_option)(argv[i]);
         if (!ok)
            VG_(usage)();
      }
      else
         VG_(usage)();
   }

#  undef ISSPACE

   if (VG_(clo_verbosity) < 0)
      VG_(clo_verbosity) = 0;

   if (VG_(clo_GDB_attach) && VG_(clo_trace_children)) {
      VG_(message)(Vg_UserMsg, "");
      VG_(message)(Vg_UserMsg, 
         "--gdb-attach=yes conflicts with --trace-children=yes");
      VG_(message)(Vg_UserMsg, 
         "Please choose one or the other, but not both.");
      VG_(bad_option)("--gdb-attach=yes and --trace-children=yes");
   }

   /* Set up logging now.  After this is done, VG_(clo_logfile_fd)
      should be connected to whatever sink has been selected, and we
      indiscriminately chuck stuff into it without worrying what the
      nature of it is.  Oh the wonder of Unix streams. */

   /* So far we should be still attached to stdout, so we can show on
      the terminal any problems to do with processing command line
      opts. */
   vg_assert(VG_(clo_logfile_fd) == 1 /* stdout */);
   vg_assert(VG_(logging_to_filedes) == True);

   switch (VG_(clo_log_to)) {

      case VgLogTo_Fd: 
         vg_assert(VG_(clo_logfile_name) == NULL);
         VG_(clo_logfile_fd) = eventually_logfile_fd;
         break;

      case VgLogTo_File: {
         Char logfilename[1000];
	 Int seq = 0;
	 Int pid = VG_(getpid)();

         vg_assert(VG_(clo_logfile_name) != NULL);
         vg_assert(VG_(strlen)(VG_(clo_logfile_name)) <= 900); /* paranoia */

	 for(;;) {
	    if (seq == 0)
	       VG_(sprintf)(logfilename, "%s.pid%d",
			    VG_(clo_logfile_name), pid );
	    else
	       VG_(sprintf)(logfilename, "%s.pid%d.%d",
			    VG_(clo_logfile_name), pid, seq );
	    seq++;

	    eventually_logfile_fd 
	       = VG_(open)(logfilename, 
			   VKI_O_CREAT|VKI_O_WRONLY|VKI_O_EXCL|VKI_O_TRUNC, 
			   VKI_S_IRUSR|VKI_S_IWUSR);
	    if (eventually_logfile_fd >= 0) {
	       VG_(clo_logfile_fd) = VG_(safe_fd)(eventually_logfile_fd);
	       break;
	    } else {
	       if (eventually_logfile_fd != -VKI_EEXIST) {
		  VG_(message)(Vg_UserMsg, 
			       "Can't create/open log file `%s.pid%d'; giving up!", 
			       VG_(clo_logfile_name), pid);
		  VG_(bad_option)(
		     "--logfile=<file> didn't work out for some reason.");
		  break;
	       }
	    }
	 }
         break;
      }

      case VgLogTo_Socket: {
         vg_assert(VG_(clo_logfile_name) != NULL);
         vg_assert(VG_(strlen)(VG_(clo_logfile_name)) <= 900); /* paranoia */
         eventually_logfile_fd 
            = VG_(connect_via_socket)( VG_(clo_logfile_name) );
         if (eventually_logfile_fd == -1) {
            VG_(message)(Vg_UserMsg, 
               "Invalid --logsocket=ipaddr or --logsocket=ipaddr:port spec"); 
            VG_(message)(Vg_UserMsg, 
               "of `%s'; giving up!", VG_(clo_logfile_name) );
            VG_(bad_option)(
               "--logsocket=");
	 }
         if (eventually_logfile_fd == -2) {
            VG_(message)(Vg_UserMsg, 
               "valgrind: failed to connect to logging server `%s'.",
               VG_(clo_logfile_name) ); 
            VG_(message)(Vg_UserMsg, 
                "Log messages will sent to stderr instead." );
            VG_(message)(Vg_UserMsg, 
                "" );
            /* We don't change anything here. */
	 } else {
            vg_assert(eventually_logfile_fd > 0);
            VG_(clo_logfile_fd) = eventually_logfile_fd;
            VG_(logging_to_filedes) = False;
         }
         break;
      }

   }

   /* Move logfile_fd into the safe range, so it doesn't conflict with any app fds */
   eventually_logfile_fd = VG_(fcntl)(VG_(clo_logfile_fd), VKI_F_DUPFD, VG_(max_fd)+1);
   if (eventually_logfile_fd < 0)
      VG_(message)(Vg_UserMsg, "valgrind: failed to move logfile fd into safe range");
   else {
      VG_(clo_logfile_fd) = eventually_logfile_fd;
      VG_(fcntl)(VG_(clo_logfile_fd), VKI_F_SETFD, VKI_FD_CLOEXEC);
   }

   /* Ok, the logging sink is running now.  Print a suitable preamble.
      If logging to file or a socket, write details of parent PID and
      command line args, to help people trying to interpret the
      results of a run which encompasses multiple processes. */

   if (VG_(clo_verbosity > 0)) {
      /* Skin details */
      VG_(message)(Vg_UserMsg, "%s%s%s, %s for x86-linux.",
                   VG_(details).name, 
                   NULL == VG_(details).version ?        "" : "-",
                   NULL == VG_(details).version 
                      ? (Char*)"" : VG_(details).version,
                   VG_(details).description);
      VG_(message)(Vg_UserMsg, "%s", VG_(details).copyright_author);

      /* Core details */
      VG_(message)(Vg_UserMsg,
         "Using valgrind-%s, a program supervision framework for x86-linux.",
         VERSION);
      VG_(message)(Vg_UserMsg, 
         "Copyright (C) 2000-2004, and GNU GPL'd, by Julian Seward.");
   }

   if (VG_(clo_verbosity) > 0 && VG_(clo_log_to) != VgLogTo_Fd) {
      VG_(message)(Vg_UserMsg, "");
      VG_(message)(Vg_UserMsg, 
         "My PID = %d, parent PID = %d.  Prog and args are:",
         VG_(getpid)(), VG_(getppid)() );
      for (i = 0; i < VG_(client_argc); i++) 
         VG_(message)(Vg_UserMsg, "   %s", VG_(client_argv)[i]);
   }

   if (VG_(clo_verbosity) > 1) {
      if (VG_(clo_log_to) != VgLogTo_Fd)
         VG_(message)(Vg_UserMsg, "");
      VG_(message)(Vg_UserMsg, "Valgrind library directory: %s", VG_(libdir));
      VG_(message)(Vg_UserMsg, "Command line");
      for (i = 0; i < VG_(client_argc); i++)
         VG_(message)(Vg_UserMsg, "   %s", VG_(client_argv)[i]);

      VG_(message)(Vg_UserMsg, "Startup, with flags:");
      for (i = 1; i < argc; i++) {
         VG_(message)(Vg_UserMsg, "   %s", argv[i]);
      }
   }

   if (VG_(clo_n_suppressions) < VG_CLO_MAX_SFILES-1 &&
       (VG_(needs).core_errors || VG_(needs).skin_errors)) {
      /* If there are no suppression files specified and the skin
	 needs one, load the default */
      static const Char default_supp[] = "default.supp";
      Int len = VG_(strlen)(VG_(libdir)) + 1 + sizeof(default_supp);
      Char *buf = VG_(arena_malloc)(VG_AR_CORE, len);
      VG_(sprintf)(buf, "%s/%s", VG_(libdir), default_supp);
      VG_(clo_suppressions)[VG_(clo_n_suppressions)] = buf;
      VG_(clo_n_suppressions)++;
   }

   if (VG_(clo_gen_suppressions) && 
       !VG_(needs).core_errors && !VG_(needs).skin_errors) {
      config_error("Can't use --gen-suppressions=yes with this skin,\n"
                   "   as it doesn't generate errors.");
   }

}

/* ---------------------------------------------------------------------
   Copying to/from m_state_static.
   ------------------------------------------------------------------ */

/* See comment about this in vg_include.h.  Change only with
   great care.
*/
__attribute__ ((aligned (16)))
UInt VG_(m_state_static) [6 /* segment regs, Intel order */
                          + 8 /* int regs, in Intel order */ 
                          + 1 /* %eflags */ 
                          + 1 /* %eip */
                          + VG_SIZE_OF_SSESTATE_W /* FPU state */
                         ];

UInt VG_(insertDflag)(UInt eflags, Int d)
{
   vg_assert(d == 1 || d == -1);
   eflags &= ~EFlagD;

   if (d < 0)
      eflags |= EFlagD;

   return eflags;
}

Int VG_(extractDflag)(UInt eflags)
{
   Int ret;

   if (eflags & EFlagD)
      ret = -1;
   else
      ret = 1;

   return ret;
}

void VG_(copy_baseBlock_to_m_state_static) ( void )
{
   Int i;
   VG_(m_state_static)[ 0/4] = VG_(baseBlock)[VGOFF_(m_cs)];
   VG_(m_state_static)[ 4/4] = VG_(baseBlock)[VGOFF_(m_ss)];
   VG_(m_state_static)[ 8/4] = VG_(baseBlock)[VGOFF_(m_ds)];
   VG_(m_state_static)[12/4] = VG_(baseBlock)[VGOFF_(m_es)];
   VG_(m_state_static)[16/4] = VG_(baseBlock)[VGOFF_(m_fs)];
   VG_(m_state_static)[20/4] = VG_(baseBlock)[VGOFF_(m_gs)];

   VG_(m_state_static)[24/4] = VG_(baseBlock)[VGOFF_(m_eax)];
   VG_(m_state_static)[28/4] = VG_(baseBlock)[VGOFF_(m_ecx)];
   VG_(m_state_static)[32/4] = VG_(baseBlock)[VGOFF_(m_edx)];
   VG_(m_state_static)[36/4] = VG_(baseBlock)[VGOFF_(m_ebx)];
   VG_(m_state_static)[40/4] = VG_(baseBlock)[VGOFF_(m_esp)];
   VG_(m_state_static)[44/4] = VG_(baseBlock)[VGOFF_(m_ebp)];
   VG_(m_state_static)[48/4] = VG_(baseBlock)[VGOFF_(m_esi)];
   VG_(m_state_static)[52/4] = VG_(baseBlock)[VGOFF_(m_edi)];

   VG_(m_state_static)[56/4] 
      = VG_(insertDflag)(VG_(baseBlock)[VGOFF_(m_eflags)],
                         VG_(baseBlock)[VGOFF_(m_dflag)]);
   VG_(m_state_static)[60/4] = VG_(baseBlock)[VGOFF_(m_eip)];

   for (i = 0; i < VG_SIZE_OF_SSESTATE_W; i++)
      VG_(m_state_static)[64/4 + i] 
         = VG_(baseBlock)[VGOFF_(m_ssestate) + i];
}


void VG_(copy_m_state_static_to_baseBlock) ( void )
{
   Int i;
   VG_(baseBlock)[VGOFF_(m_cs)] = VG_(m_state_static)[ 0/4];
   VG_(baseBlock)[VGOFF_(m_ss)] = VG_(m_state_static)[ 4/4];
   VG_(baseBlock)[VGOFF_(m_ds)] = VG_(m_state_static)[ 8/4];
   VG_(baseBlock)[VGOFF_(m_es)] = VG_(m_state_static)[12/4];
   VG_(baseBlock)[VGOFF_(m_fs)] = VG_(m_state_static)[16/4];
   VG_(baseBlock)[VGOFF_(m_gs)] = VG_(m_state_static)[20/4];

   VG_(baseBlock)[VGOFF_(m_eax)] = VG_(m_state_static)[24/4];
   VG_(baseBlock)[VGOFF_(m_ecx)] = VG_(m_state_static)[28/4];
   VG_(baseBlock)[VGOFF_(m_edx)] = VG_(m_state_static)[32/4];
   VG_(baseBlock)[VGOFF_(m_ebx)] = VG_(m_state_static)[36/4];
   VG_(baseBlock)[VGOFF_(m_esp)] = VG_(m_state_static)[40/4];
   VG_(baseBlock)[VGOFF_(m_ebp)] = VG_(m_state_static)[44/4];
   VG_(baseBlock)[VGOFF_(m_esi)] = VG_(m_state_static)[48/4];
   VG_(baseBlock)[VGOFF_(m_edi)] = VG_(m_state_static)[52/4];

   VG_(baseBlock)[VGOFF_(m_eflags)] 
      = VG_(m_state_static)[56/4] & ~EFlagD;
   VG_(baseBlock)[VGOFF_(m_dflag)] 
      = VG_(extractDflag)(VG_(m_state_static)[56/4]);

   VG_(baseBlock)[VGOFF_(m_eip)] = VG_(m_state_static)[60/4];

   for (i = 0; i < VG_SIZE_OF_SSESTATE_W; i++)
      VG_(baseBlock)[VGOFF_(m_ssestate) + i]
         = VG_(m_state_static)[64/4 + i];
}

Addr VG_(get_stack_pointer) ( void )
{
   return VG_(baseBlock)[VGOFF_(m_esp)];
}

/* ---------------------------------------------------------------------
   Show accumulated counts.
   ------------------------------------------------------------------ */

static __inline__ Int safe_idiv(Int a, Int b)
{
   return (b == 0 ? 0 : a / b);
}

static void vg_show_counts ( void )
{
   VG_(message)(Vg_DebugMsg,
		"    TT/TC: %d tc sectors discarded.",
                VG_(number_of_tc_discards) );
   VG_(message)(Vg_DebugMsg,
                "           %d chainings, %d unchainings.",
                VG_(bb_enchain_count), VG_(bb_dechain_count) );
   VG_(message)(Vg_DebugMsg,
                "translate: new     %d (%d -> %d; ratio %d:10)",
                VG_(overall_in_count),
                VG_(overall_in_osize),
                VG_(overall_in_tsize),
                safe_idiv(10*VG_(overall_in_tsize), VG_(overall_in_osize)));
   VG_(message)(Vg_DebugMsg,
                "           discard %d (%d -> %d; ratio %d:10).",
                VG_(overall_out_count),
                VG_(overall_out_osize),
                VG_(overall_out_tsize),
                safe_idiv(10*VG_(overall_out_tsize), VG_(overall_out_osize)));
   VG_(message)(Vg_DebugMsg,
      " dispatch: %llu jumps (bb entries), of which %u (%lu%%) were unchained.",
      VG_(bbs_done), 
      VG_(unchained_jumps_done),
      ((ULong)(100) * (ULong)(VG_(unchained_jumps_done)))
         / ( VG_(bbs_done)==0 ? 1 : VG_(bbs_done) )
   );

   VG_(message)(Vg_DebugMsg,
      "           %d/%d major/minor sched events.  %d tt_fast misses.", 
                     VG_(num_scheduling_events_MAJOR), 
                     VG_(num_scheduling_events_MINOR), 
                     VG_(tt_fast_misses));

   VG_(message)(Vg_DebugMsg, 
                "reg-alloc: %d t-req-spill, "
                "%d+%d orig+spill uis, %d total-reg-r.",
                VG_(translations_needing_spill),
                VG_(uinstrs_prealloc),
                VG_(uinstrs_spill),
                VG_(total_reg_rank) );
   VG_(message)(Vg_DebugMsg, 
                "   sanity: %d cheap, %d expensive checks.",
                VG_(sanity_fast_count), 
                VG_(sanity_slow_count) );
   VG_(print_ccall_stats)();
}


/* ---------------------------------------------------------------------
   Main!
   ------------------------------------------------------------------ */

/* Initialize the PID and PGRP of scheduler LWP; this is also called
   in any new children after fork. */
static void newpid(ThreadId unused)
{
   /* PID of scheduler LWP */
   VG_(main_pid) = VG_(getpid)();
   VG_(main_pgrp) = VG_(getpgrp)();
}

/* Where we jump to once Valgrind has got control, and the real
   machine's state has been copied to the m_state_static. */

void VG_(main) ( const KickstartParams *kp, void (*tool_init)(void), void *tool_dlhandle )
{
   VgSchedReturnCode src;
   struct vki_rlimit rl;

   /* initial state */
   if (0)
      VG_(printf)("starting esp=%p eip=%p, esp=%p\n", kp->client_esp, kp->client_eip, &src);
   VG_(esp_at_startup) = kp->client_esp;
   VG_(memset)(&VG_(m_state_static), 0, sizeof(VG_(m_state_static)));
   VG_(m_state_static)[40/4] = kp->client_esp;
   VG_(m_state_static)[60/4] = kp->client_eip;

   /* set up an initial FPU state (doesn't really matter what it is,
      so long as it's somewhat valid) */
   if (!VG_(have_ssestate))
	   asm volatile("fwait; fnsave %0; fwait; frstor %0; fwait" 
			: : "m" (VG_(m_state_static)[64/4]) : "cc", "memory");
   else
	   asm volatile("fwait; fxsave %0; fwait; andl $0xffbf, %1; fxrstor %0; fwait"
			: : "m" (VG_(m_state_static)[64/4]), "m" (VG_(m_state_static)[(64+24)/4]) : "cc", "memory");

   VG_(brk_base)          = VG_(brk_limit) = kp->client_brkbase;
   VG_(client_base)       = kp->client_base;
   VG_(client_end)        = kp->client_end;
   VG_(client_mapbase)    = kp->client_mapbase;
   VG_(clstk_base)        = kp->clstk_base;
   VG_(clstk_end)         = kp->clstk_end;
   vg_assert(VG_(clstk_end) == VG_(client_end));

   VG_(shadow_base)	  = kp->shadow_base;
   VG_(shadow_end)	  = kp->shadow_end;
   VG_(valgrind_base)	  = kp->vg_base;
   VG_(valgrind_mmap_end) = kp->vg_mmap_end;
   VG_(valgrind_end)	  = kp->vg_end;

   VG_(libdir)            = kp->libdir;

   VG_(client_trampoline_code) = kp->cl_tramp_code;

   if (0) {
      if (VG_(have_ssestate))
         VG_(printf)("Looks like a SSE-capable CPU\n");
      else
         VG_(printf)("Looks like a MMX-only CPU\n");
   }

   VG_(atfork)(NULL, NULL, newpid);
   newpid(VG_INVALID_THREADID);

   /* Get the current file descriptor limits. */
   if (VG_(getrlimit)(VKI_RLIMIT_NOFILE, &rl) < 0) {
      rl.rlim_cur = 1024;
      rl.rlim_max = 1024;
   }

   /* Work out where to move the soft limit to. */
   if (rl.rlim_cur + VG_N_RESERVED_FDS <= rl.rlim_max) {
      rl.rlim_cur = rl.rlim_cur + VG_N_RESERVED_FDS;
   } else {
      rl.rlim_cur = rl.rlim_max;
   }

   /* Reserve some file descriptors for our use. */
   VG_(max_fd) = rl.rlim_cur - VG_N_RESERVED_FDS;

   /* Update the soft limit. */
   VG_(setrlimit)(VKI_RLIMIT_NOFILE, &rl);

   if (kp->vgexecfd != -1)
      VG_(vgexecfd) = VG_(safe_fd)(kp->vgexecfd);
   if (kp->clexecfd != -1)
      VG_(clexecfd) = VG_(safe_fd)(kp->clexecfd);

   /* Read /proc/self/maps into a buffer.  Must be before:
      - SK_(pre_clo_init)(): so that if it calls VG_(malloc)(), any mmap'd
        superblocks are not erroneously identified as being owned by the
        client, which would be bad.
      - init_memory(): that's where the buffer is parsed
      - init_tt_tc(): so the anonymous mmaps for the translation table and
        translation cache aren't identified as part of the client, which would
        waste > 20M of virtual address space, and be bad.
   */
   VG_(read_procselfmaps)();

   /* Setup stuff that depends on the skin.  Must be before:
      - vg_init_baseBlock(): to register helpers
      - process_cmd_line_options(): to register skin name and description,
        and turn on/off 'command_line_options' need
      - init_memory() (to setup memory event trackers).
   */
   (*tool_init)();
   VG_(tool_init_dlsym)(tool_dlhandle);

   VG_(sanity_check_needs)();

   /* Process Valgrind's command-line opts */
   process_cmd_line_options(kp);

   /* Hook to delay things long enough so we can get the pid and
      attach GDB in another shell. */
   if (VG_(clo_wait_for_gdb)) {
      VG_(printf)("pid=%d\n", VG_(getpid)());
      /* do "jump *$eip" to skip this in gdb */
      VG_(do_syscall)(__NR_pause);
   }

   /* Do post command-line processing initialisation.  Must be before:
      - vg_init_baseBlock(): to register any more helpers
   */
   SK_(post_clo_init)();

   /* Set up baseBlock offsets and copy the saved machine's state into it. */
   vg_init_baseBlock();

   /* Search for file descriptors that are inherited from our parent. */
   if (VG_(clo_track_fds))
      VG_(init_preopened_fds)();

   /* Initialise the scheduler, and copy the client's state from
      baseBlock into VG_(threads)[1].  Must be before:
      - VG_(sigstartup_actions)()
   */
   VG_(scheduler_init)();

   /* Set up the ProxyLWP machinery */
   VG_(proxy_init)();

   /* Initialise the signal handling subsystem, temporarily parking
      the saved blocking-mask in saved_sigmask. */
   VG_(sigstartup_actions)();

   /* Perhaps we're profiling Valgrind? */
   if (VG_(clo_profile))
      VGP_(init_profiling)();

   /* Start calibration of our RDTSC-based clock. */
   VG_(start_rdtsc_calibration)();

   /* Parse /proc/self/maps to learn about startup segments. */
   VGP_PUSHCC(VgpInitMem);
   VG_(init_memory)();
   VGP_POPCC(VgpInitMem);

   /* Read the list of errors to suppress.  This should be found in
      the file specified by vg_clo_suppressions. */
   if (VG_(needs).core_errors || VG_(needs).skin_errors)
      VG_(load_suppressions)();

   /* End calibration of our RDTSC-based clock, leaving it as long as
      we can. */
   VG_(end_rdtsc_calibration)();

   /* Initialise translation table and translation cache. */
   VG_(init_tt_tc)();

   if (VG_(clo_verbosity) == 1) {
      VG_(message)(Vg_UserMsg, 
                   "For more details, rerun with: -v");
   }

   /* Force a read of the debug info so that we can look for 
      glibc entry points to intercept. */
   VG_(setup_code_redirect_table)();

   /* Now it is safe for malloc et al in vg_clientmalloc.c to act
      instrumented-ly. */
   if (VG_(clo_verbosity) > 0)
      VG_(message)(Vg_UserMsg, "");

   VG_(bbs_to_go) = VG_(clo_stop_after);

   if (VG_(clo_pointercheck)) {
      vki_modify_ldt_t ldt = { VG_POINTERCHECK_SEGIDX,
			       VG_(client_base),
			       (VG_(client_end)-VG_(client_base)) / VKI_BYTES_PER_PAGE,
			       1,		/* 32 bit */
			       0,		/* contents: data, RW, non-expanding */
			       0,		/* not read-exec only */
			       1,		/* limit in pages */
			       0,		/* !seg not present */
			       1,		/* usable */
      };
      Int ret = VG_(do_syscall)(__NR_modify_ldt, 1, &ldt, sizeof(ldt));

      if (ret < 0) {
	 VG_(message)(Vg_UserMsg,
		      "Warning: ignoring --pointercheck=yes, "
		      "because modify_ldt failed (errno=%d)", -ret);
	 VG_(clo_pointercheck) = False;
      }
   }

   /* Run! */
   VG_(running_on_simd_CPU) = True;
   VGP_PUSHCC(VgpSched);

   if (__builtin_setjmp(&VG_(fatal_signal_jmpbuf)) == 0) {
      VG_(fatal_signal_set) = True;
      src = VG_(scheduler)();
   } else
      src = VgSrc_FatalSig;

   VGP_POPCC(VgpSched);
   VG_(running_on_simd_CPU) = False;

   if (VG_(clo_verbosity) > 0)
      VG_(message)(Vg_UserMsg, "");

   if (src == VgSrc_Deadlock) {
     VG_(message)(Vg_UserMsg, 
        "Warning: pthread scheduler exited due to deadlock");
   }

   /* Print out file descriptor summary and stats. */
   if(VG_(clo_track_fds))
      VG_(fd_stats)();

   if (VG_(needs).core_errors || VG_(needs).skin_errors)
      VG_(show_all_errors)();

   SK_(fini)( VG_(exitcode) );

   VG_(do_sanity_checks)( True /*include expensive checks*/ );

   if (VG_(clo_verbosity) > 1)
      vg_show_counts();

   if (VG_(clo_verbosity) > 3)
      VG_(print_UInstr_histogram)();

   if (0) {
      VG_(message)(Vg_DebugMsg, "");
      VG_(message)(Vg_DebugMsg, 
         "------ Valgrind's internal memory use stats follow ------" );
      VG_(mallocSanityCheckAll)();
      VG_(show_all_arena_stats)();
      VG_(message)(Vg_DebugMsg, 
         "------ Valgrind's ExeContext management stats follow ------" );
      VG_(show_ExeContext_stats)();
   }
 
   if (VG_(clo_profile))
      VGP_(done_profiling)();

   VG_(shutdown_logging)();

   /* We're exiting, so nuke all the threads and clean up the proxy LWPs */
   vg_assert(src == VgSrc_FatalSig ||
	     VG_(threads)[VG_(last_run_tid)].status == VgTs_Runnable ||
	     VG_(threads)[VG_(last_run_tid)].status == VgTs_WaitJoiner);
   VG_(nuke_all_threads_except)(VG_INVALID_THREADID);

   /* Decide how to exit.  This depends on what the scheduler
      returned. */
  
   switch (src) {
      case VgSrc_ExitSyscall: /* the normal way out */
         vg_assert(VG_(last_run_tid) > 0 
                   && VG_(last_run_tid) < VG_N_THREADS);
	 VG_(proxy_shutdown)();

         /* The thread's %EBX at the time it did __NR_exit() will hold
            the arg to __NR_exit(), so we just do __NR_exit() with
            that arg. */
         VG_(exit)( VG_(exitcode) );
         /* NOT ALIVE HERE! */
         VG_(core_panic)("entered the afterlife in vg_main() -- ExitSyscall");
         break; /* what the hell :) */

      case VgSrc_Deadlock:
         /* Just exit now.  No point in continuing. */
	 VG_(proxy_shutdown)();
         VG_(exit)(0);
         VG_(core_panic)("entered the afterlife in vg_main() -- Deadlock");
         break;

      case VgSrc_BbsDone: 
         /* Tricky; we have to try and switch back to the real CPU.
            This is all very dodgy and won't work at all in the
            presence of threads, or if the client happened to be
            running a signal handler. */
         /* Prepare to restore state to the real CPU. */
         VG_(sigshutdown_actions)();
         VG_(load_thread_state)(1 /* root thread */ );
         VG_(copy_baseBlock_to_m_state_static)();

	 VG_(proxy_shutdown)();

         /* This pushes a return address on the simulator's stack,
            which is abandoned.  We call vg_sigshutdown_actions() at
            the end of vg_switch_to_real_CPU(), so as to ensure that
            the original stack and machine state is restored before
            the real signal mechanism is restored.  */
         VG_(switch_to_real_CPU)();

      case VgSrc_FatalSig:
	 /* We were killed by a fatal signal, so replicate the effect */
	 vg_assert(VG_(fatal_sigNo) != -1);
	 VG_(kill_self)(VG_(fatal_sigNo));
	 VG_(core_panic)("vg_main(): signal was supposed to be fatal");
	 break;

      default:
         VG_(core_panic)("vg_main(): unexpected scheduler return code");
   }
}


/* Debugging thing .. can be called from assembly with OYNK macro. */
void VG_(oynk) ( Int n )
{
   OINK(n);
}


/* Walk through a colon-separated environment variable, and remove the
   entries which matches file_pattern.  It slides everything down over
   the removed entries, and pads the remaining space with '\0'.  It
   modifies the entries in place (in the client address space), but it
   shouldn't matter too much, since we only do this just before an
   execve().

   This is also careful to mop up any excess ':'s, since empty strings
   delimited by ':' are considered to be '.' in a path.
*/
void VG_(mash_colon_env)(Char *varp, const Char *remove_pattern)
{
   Char *const start = varp;
   Char *entry_start = varp;
   Char *output = varp;

   if (varp == NULL)
      return;

   while(*varp) {
      if (*varp == ':') {
	 Char prev;
	 Bool match;

	 /* This is a bit subtle: we want to match against the entry
	    we just copied, because it may have overlapped with
	    itself, junking the original. */

	 prev = *output;
	 *output = '\0';

	 match = VG_(string_match)(remove_pattern, entry_start);

	 *output = prev;
	 
	 if (match) {
	    output = entry_start;
	    varp++;			/* skip ':' after removed entry */
	 } else
	    entry_start = output+1;	/* entry starts after ':' */
      }

      *output++ = *varp++;
   }

   /* match against the last entry */
   if (VG_(string_match)(remove_pattern, entry_start)) {
      output = entry_start;
      if (output > start) {
	 /* remove trailing ':' */
	 output--;
	 vg_assert(*output == ':');
      }
   }	 

   /* pad out the left-overs with '\0' */
   while(output < varp)
      *output++ = '\0';
}

/* Start GDB and get it to attach to this process.  Called if the user
   requests this service after an error has been shown, so she can
   poke around and look at parameters, memory, etc.  You can't
   meaningfully get GDB to continue the program, though; to continue,
   quit GDB.  */
void VG_(start_GDB) ( Int tid )
{
   Int pid;

   if ((pid = fork()) == 0)
   {
      ptrace(PTRACE_TRACEME, 0, NULL, NULL);
      VG_(kkill)(VG_(getpid)(), VKI_SIGSTOP);
   }
   else if (pid > 0) 
   {
      struct user_regs_struct regs;
      Int status;
      Int res;

      if (VG_(is_running_thread)( tid )) {
         regs.xcs = VG_(baseBlock)[VGOFF_(m_cs)];
         regs.xss = VG_(baseBlock)[VGOFF_(m_ss)];
         regs.xds = VG_(baseBlock)[VGOFF_(m_ds)];
         regs.xes = VG_(baseBlock)[VGOFF_(m_es)];
         regs.xfs = VG_(baseBlock)[VGOFF_(m_fs)];
         regs.xgs = VG_(baseBlock)[VGOFF_(m_gs)];
         regs.eax = VG_(baseBlock)[VGOFF_(m_eax)];
         regs.ebx = VG_(baseBlock)[VGOFF_(m_ebx)];
         regs.ecx = VG_(baseBlock)[VGOFF_(m_ecx)];
         regs.edx = VG_(baseBlock)[VGOFF_(m_edx)];
         regs.esi = VG_(baseBlock)[VGOFF_(m_esi)];
         regs.edi = VG_(baseBlock)[VGOFF_(m_edi)];
         regs.ebp = VG_(baseBlock)[VGOFF_(m_ebp)];
         regs.esp = VG_(baseBlock)[VGOFF_(m_esp)];
         regs.eflags = VG_(baseBlock)[VGOFF_(m_eflags)];
         regs.eip = VG_(baseBlock)[VGOFF_(m_eip)];
      } else {
         ThreadState* tst = & VG_(threads)[ tid ];
         
         regs.xcs = tst->m_cs;
         regs.xss = tst->m_ss;
         regs.xds = tst->m_ds;
         regs.xes = tst->m_es;
         regs.xfs = tst->m_fs;
         regs.xgs = tst->m_gs;
         regs.eax = tst->m_eax;
         regs.ebx = tst->m_ebx;
         regs.ecx = tst->m_ecx;
         regs.edx = tst->m_edx;
         regs.esi = tst->m_esi;
         regs.edi = tst->m_edi;
         regs.ebp = tst->m_ebp;
         regs.esp = tst->m_esp;
         regs.eflags = tst->m_eflags;
         regs.eip = tst->m_eip;
      }

      if ((res = VG_(waitpid)(pid, &status, 0)) == pid &&
          WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP &&
          ptrace(PTRACE_SETREGS, pid, NULL, &regs) == 0 &&
          ptrace(PTRACE_DETACH, pid, NULL, SIGSTOP) == 0) {
         UChar buf[VG_(strlen)(VG_(clo_GDB_path)) + 100];

         VG_(sprintf)(buf, "%s -nw /proc/%d/fd/%d %d",
                      VG_(clo_GDB_path), VG_(main_pid), VG_(clexecfd), pid);
         VG_(message)(Vg_UserMsg, "starting GDB with cmd: %s", buf);
         res = VG_(system)(buf);
         if (res == 0) {      
            VG_(message)(Vg_UserMsg, "");
            VG_(message)(Vg_UserMsg, 
                         "GDB has detached.  Valgrind regains control.  We continue.");
         } else {
            VG_(message)(Vg_UserMsg, "Apparently failed!");
            VG_(message)(Vg_UserMsg, "");
         }
      }

      VG_(kkill)(pid, VKI_SIGKILL);
      VG_(waitpid)(pid, &status, 0);
   }
}


/* Print some helpful-ish text about unimplemented things, and give
   up. */
void VG_(unimplemented) ( Char* msg )
{
   VG_(message)(Vg_UserMsg, "");
   VG_(message)(Vg_UserMsg, 
      "Valgrind detected that your program requires");
   VG_(message)(Vg_UserMsg, 
      "the following unimplemented functionality:");
   VG_(message)(Vg_UserMsg, "   %s", msg);
   VG_(message)(Vg_UserMsg,
      "This may be because the functionality is hard to implement,");
   VG_(message)(Vg_UserMsg,
      "or because no reasonable program would behave this way,");
   VG_(message)(Vg_UserMsg,
      "or because nobody has yet needed it.  In any case, let us know at");
   VG_(message)(Vg_UserMsg,
      "%s and/or try to work around the problem, if you can.", VG_BUGS_TO);
   VG_(message)(Vg_UserMsg,
      "");
   VG_(message)(Vg_UserMsg,
      "Valgrind has to exit now.  Sorry.  Bye!");
   VG_(message)(Vg_UserMsg,
      "");
   VG_(pp_sched_status)();
   VG_(exit)(1);
}


/* ---------------------------------------------------------------------
   Sanity check machinery (permanently engaged).
   ------------------------------------------------------------------ */

/* A fast sanity check -- suitable for calling circa once per
   millisecond. */

void VG_(do_sanity_checks) ( Bool force_expensive )
{
   VGP_PUSHCC(VgpCoreCheapSanity);

   if (VG_(sanity_level) < 1) return;

   /* --- First do all the tests that we can do quickly. ---*/

   VG_(sanity_fast_count)++;

   /* Check stuff pertaining to the memory check system. */

   /* Check that nobody has spuriously claimed that the first or
      last 16 pages of memory have become accessible [...] */
   if (VG_(needs).sanity_checks) {
      VGP_PUSHCC(VgpSkinCheapSanity);
      vg_assert(SK_(cheap_sanity_check)());
      VGP_POPCC(VgpSkinCheapSanity);
   }

   /* --- Now some more expensive checks. ---*/

   /* Once every 25 times, check some more expensive stuff. */
   if ( force_expensive
     || VG_(sanity_level) > 1
     || (VG_(sanity_level) == 1 && (VG_(sanity_fast_count) % 25) == 0)) {

      VGP_PUSHCC(VgpCoreExpensiveSanity);
      VG_(sanity_slow_count)++;

      VG_(proxy_sanity)();

#     if 0
      { void zzzmemscan(void); zzzmemscan(); }
#     endif

      if ((VG_(sanity_fast_count) % 250) == 0)
         VG_(sanity_check_tc_tt)();

      if (VG_(needs).sanity_checks) {
          VGP_PUSHCC(VgpSkinExpensiveSanity);
          vg_assert(SK_(expensive_sanity_check)());
          VGP_POPCC(VgpSkinExpensiveSanity);
      }
      /* 
      if ((VG_(sanity_fast_count) % 500) == 0) VG_(mallocSanityCheckAll)(); 
      */
      VGP_POPCC(VgpCoreExpensiveSanity);
   }

   if (VG_(sanity_level) > 1) {
      VGP_PUSHCC(VgpCoreExpensiveSanity);
      /* Check sanity of the low-level memory manager.  Note that bugs
         in the client's code can cause this to fail, so we don't do
         this check unless specially asked for.  And because it's
         potentially very expensive. */
      VG_(mallocSanityCheckAll)();
      VGP_POPCC(VgpCoreExpensiveSanity);
   }
   VGP_POPCC(VgpCoreCheapSanity);
}
/*--------------------------------------------------------------------*/
/*--- end                                                vg_main.c ---*/
/*--------------------------------------------------------------------*/


/*--------------------------------------------------------------------*/
/*--- C startup stuff, reached from vg_startup.S.                  ---*/
/*---                                                    vg_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, an extensible x86 protected-mode
   emulator for monitoring program execution on x86-Unixes.

   Copyright (C) 2000-2002 Julian Seward 
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
Int VGOFF_(m_fpustate) = INVALID_OFFSET;
Int VGOFF_(ldt)   = INVALID_OFFSET;
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
Int VGOFF_(helper_RDTSC) = INVALID_OFFSET;
Int VGOFF_(helper_CPUID) = INVALID_OFFSET;
Int VGOFF_(helper_BSWAP) = INVALID_OFFSET;
Int VGOFF_(helper_bsf) = INVALID_OFFSET;
Int VGOFF_(helper_bsr) = INVALID_OFFSET;
Int VGOFF_(helper_fstsw_AX) = INVALID_OFFSET;
Int VGOFF_(helper_SAHF) = INVALID_OFFSET;
Int VGOFF_(helper_DAS) = INVALID_OFFSET;
Int VGOFF_(helper_DAA) = INVALID_OFFSET;
Int VGOFF_(handle_esp_assignment) = INVALID_OFFSET;

/* MAX_NONCOMPACT_HELPERS can be increased easily.  If MAX_COMPACT_HELPERS is
 * increased too much, they won't really be compact any more... */
#define  MAX_COMPACT_HELPERS     8
#define  MAX_NONCOMPACT_HELPERS  8 

UInt VG_(n_compact_helpers)    = 0;
UInt VG_(n_noncompact_helpers) = 0;

Addr VG_(compact_helper_addrs)  [MAX_COMPACT_HELPERS];
Int  VG_(compact_helper_offsets)[MAX_COMPACT_HELPERS];
Addr VG_(noncompact_helper_addrs)  [MAX_NONCOMPACT_HELPERS];
Int  VG_(noncompact_helper_offsets)[MAX_NONCOMPACT_HELPERS];

/* This is the actual defn of baseblock. */
UInt VG_(baseBlock)[VG_BASEBLOCK_WORDS];


/* Words. */
static Int baB_off = 0;

/* Returns the offset, in words. */
static Int alloc_BaB ( Int words )
{
   Int off = baB_off;
   baB_off += words;
   if (baB_off >= VG_BASEBLOCK_WORDS)
      VG_(core_panic)( "alloc_BaB: baseBlock is too small");

   return off;   
}

/* Allocate 1 word in baseBlock and set it to the given value. */
static Int alloc_BaB_1_set ( Addr a )
{
   Int off = alloc_BaB(1);
   VG_(baseBlock)[off] = (UInt)a;
   return off;
}

/* Registers a function in compact_helper_addrs;  compact_helper_offsets is
 * filled in later.
 */
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
static void assign_helpers_in_baseBlock(UInt n, Int offsets[], Addr addrs[])
{
   Int i;
   for (i = 0; i < n; i++) offsets[i] = alloc_BaB_1_set( addrs[i] );
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

   /* (9 or 18) + n_compact_helpers  */
   /* Register VG_(handle_esp_assignment) if needed. */
   if (VG_(track_events).new_mem_stack_aligned || 
       VG_(track_events).die_mem_stack_aligned) 
      VG_(register_compact_helper)( (Addr) & VG_(handle_esp_assignment) );

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

   VGOFF_(m_fpustate) = alloc_BaB(VG_SIZE_OF_FPUSTATE_W);

   /* This thread's LDT pointer, and segment registers. */
   VGOFF_(ldt)   = alloc_BaB(1);
   VGOFF_(m_cs)  = alloc_BaB(1);
   VGOFF_(m_ss)  = alloc_BaB(1);
   VGOFF_(m_ds)  = alloc_BaB(1);
   VGOFF_(m_es)  = alloc_BaB(1);
   VGOFF_(m_fs)  = alloc_BaB(1);
   VGOFF_(m_gs)  = alloc_BaB(1);

   VG_(register_noncompact_helper)( (Addr) & VG_(do_useseg) );

   /* Helper functions. */
   VGOFF_(helper_idiv_64_32)
      = alloc_BaB_1_set( (Addr) & VG_(helper_idiv_64_32) );
   VGOFF_(helper_div_64_32)
      = alloc_BaB_1_set( (Addr) & VG_(helper_div_64_32) );
   VGOFF_(helper_idiv_32_16)
      = alloc_BaB_1_set( (Addr) & VG_(helper_idiv_32_16) );
   VGOFF_(helper_div_32_16)
      = alloc_BaB_1_set( (Addr) & VG_(helper_div_32_16) );
   VGOFF_(helper_idiv_16_8)
      = alloc_BaB_1_set( (Addr) & VG_(helper_idiv_16_8) );
   VGOFF_(helper_div_16_8)
      = alloc_BaB_1_set( (Addr) & VG_(helper_div_16_8) );

   VGOFF_(helper_imul_32_64)
      = alloc_BaB_1_set( (Addr) & VG_(helper_imul_32_64) );
   VGOFF_(helper_mul_32_64)
      = alloc_BaB_1_set( (Addr) & VG_(helper_mul_32_64) );
   VGOFF_(helper_imul_16_32)
      = alloc_BaB_1_set( (Addr) & VG_(helper_imul_16_32) );
   VGOFF_(helper_mul_16_32)
      = alloc_BaB_1_set( (Addr) & VG_(helper_mul_16_32) );
   VGOFF_(helper_imul_8_16)
      = alloc_BaB_1_set( (Addr) & VG_(helper_imul_8_16) );
   VGOFF_(helper_mul_8_16)
      = alloc_BaB_1_set( (Addr) & VG_(helper_mul_8_16) );

   VGOFF_(helper_CLD)
      = alloc_BaB_1_set( (Addr) & VG_(helper_CLD) );
   VGOFF_(helper_STD)
      = alloc_BaB_1_set( (Addr) & VG_(helper_STD) );
   VGOFF_(helper_get_dirflag)
      = alloc_BaB_1_set( (Addr) & VG_(helper_get_dirflag) );

   VGOFF_(helper_CLC)
      = alloc_BaB_1_set( (Addr) & VG_(helper_CLC) );
    VGOFF_(helper_STC)
      = alloc_BaB_1_set( (Addr) & VG_(helper_STC) );

   VGOFF_(helper_shldl)
      = alloc_BaB_1_set( (Addr) & VG_(helper_shldl) );
   VGOFF_(helper_shldw)
      = alloc_BaB_1_set( (Addr) & VG_(helper_shldw) );
   VGOFF_(helper_shrdl)
      = alloc_BaB_1_set( (Addr) & VG_(helper_shrdl) );
   VGOFF_(helper_shrdw)
      = alloc_BaB_1_set( (Addr) & VG_(helper_shrdw) );

   VGOFF_(helper_RDTSC)
      = alloc_BaB_1_set( (Addr) & VG_(helper_RDTSC) );
   VGOFF_(helper_CPUID)
      = alloc_BaB_1_set( (Addr) & VG_(helper_CPUID) );

   VGOFF_(helper_bsf)
      = alloc_BaB_1_set( (Addr) & VG_(helper_bsf) );
   VGOFF_(helper_bsr)
      = alloc_BaB_1_set( (Addr) & VG_(helper_bsr) );

   VGOFF_(helper_fstsw_AX)
      = alloc_BaB_1_set( (Addr) & VG_(helper_fstsw_AX) );
   VGOFF_(helper_SAHF)
      = alloc_BaB_1_set( (Addr) & VG_(helper_SAHF) );
   VGOFF_(helper_DAS)
      = alloc_BaB_1_set( (Addr) & VG_(helper_DAS) );
   VGOFF_(helper_DAA)
      = alloc_BaB_1_set( (Addr) & VG_(helper_DAA) );

   /* Allocate slots for noncompact helpers */
   assign_helpers_in_baseBlock(VG_(n_noncompact_helpers), 
                               VG_(noncompact_helper_offsets), 
                               VG_(noncompact_helper_addrs));
}

static void vg_init_shadow_regs ( void )
{
   if (VG_(needs).shadow_regs) {
      UInt eflags;
   
      SK_(written_shadow_regs_values) ( & VG_(written_shadow_reg), & eflags );
      VG_(baseBlock)[VGOFF_(sh_esp)]    = 
      VG_(baseBlock)[VGOFF_(sh_ebp)]    =
      VG_(baseBlock)[VGOFF_(sh_eax)]    =
      VG_(baseBlock)[VGOFF_(sh_ecx)]    =
      VG_(baseBlock)[VGOFF_(sh_edx)]    =
      VG_(baseBlock)[VGOFF_(sh_ebx)]    =
      VG_(baseBlock)[VGOFF_(sh_esi)]    =
      VG_(baseBlock)[VGOFF_(sh_edi)]    = VG_(written_shadow_reg);
      VG_(baseBlock)[VGOFF_(sh_eflags)] = eflags;

   } else
      VG_(written_shadow_reg) = VG_UNUSED_SHADOW_REG_VALUE;
}


/* ---------------------------------------------------------------------
   Global entities which are not referenced from generated code.
   ------------------------------------------------------------------ */

/* The stack on which Valgrind runs.  We can't use the same stack as
   the simulatee -- that's an important design decision.  */
UInt VG_(stack)[10000];

/* Ditto our signal delivery stack. */
UInt VG_(sigstack)[10000];

/* Saving stuff across system calls. */
UInt VG_(real_fpu_state_saved_over_syscall)[VG_SIZE_OF_FPUSTATE_W];
Addr VG_(esp_saved_over_syscall);

/* Counts downwards in vg_run_innerloop. */
UInt VG_(dispatch_ctr);


/* 64-bit counter for the number of basic blocks done. */
ULong VG_(bbs_done);
/* 64-bit counter for the number of bbs to go before a debug exit. */
ULong VG_(bbs_to_go);

/* The current LRU epoch. */
UInt VG_(current_epoch) = 0;

/* This is the ThreadId of the last thread the scheduler ran. */
ThreadId VG_(last_run_tid) = 0;

/* This is the argument to __NR_exit() supplied by the first thread to
   call that syscall.  We eventually pass that to __NR_exit() for
   real. */
UInt VG_(exitcode) = 0;


/* ---------------------------------------------------------------------
   Counters, for informational purposes only.
   ------------------------------------------------------------------ */

/* Number of lookups which miss the fast tt helper. */
UInt VG_(tt_fast_misses) = 0;


/* Counts for LRU informational messages. */

/* Number and total o/t size of new translations this epoch. */
UInt VG_(this_epoch_in_count) = 0;
UInt VG_(this_epoch_in_osize) = 0;
UInt VG_(this_epoch_in_tsize) = 0;
/* Number and total o/t size of discarded translations this epoch. */
UInt VG_(this_epoch_out_count) = 0;
UInt VG_(this_epoch_out_osize) = 0;
UInt VG_(this_epoch_out_tsize) = 0;
/* Number and total o/t size of translations overall. */
UInt VG_(overall_in_count) = 0;
UInt VG_(overall_in_osize) = 0;
UInt VG_(overall_in_tsize) = 0;
/* Number and total o/t size of discards overall. */
UInt VG_(overall_out_count) = 0;
UInt VG_(overall_out_osize) = 0;
UInt VG_(overall_out_tsize) = 0;

/* The number of LRU-clearings of TT/TC. */
UInt VG_(number_of_lrus) = 0;


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
   Skin data structure initialisation
   ------------------------------------------------------------------ */

/* Init with default values. */
VgNeeds VG_(needs) = {
   .name                    = NULL,
   .description             = NULL,
   .bug_reports_to          = NULL,

   .core_errors             = False,
   .skin_errors             = False,
   .run_libc_freeres        = False,

   .sizeof_shadow_block     = 0,

   .basic_block_discards    = False,
   .shadow_regs             = False,
   .command_line_options    = False,
   .client_requests         = False,
   .extended_UCode          = False,
   .syscall_wrapper         = False,
   .alternative_free        = False,
   .sanity_checks           = False,
};

VgTrackEvents VG_(track_events) = {
   /* Memory events */
   .new_mem_startup       = NULL,
   .new_mem_heap          = NULL,
   .new_mem_stack         = NULL,
   .new_mem_stack_aligned = NULL,
   .new_mem_stack_signal  = NULL,
   .new_mem_brk           = NULL,
   .new_mem_mmap          = NULL,

   .copy_mem_heap         = NULL,
   .change_mem_mprotect   = NULL,

   .ban_mem_heap          = NULL,
   .ban_mem_stack         = NULL,

   .die_mem_heap          = NULL,
   .die_mem_stack         = NULL,
   .die_mem_stack_aligned = NULL,
   .die_mem_stack_signal  = NULL,
   .die_mem_brk           = NULL,
   .die_mem_munmap        = NULL,

   .bad_free              = NULL,
   .mismatched_free       = NULL,

   .pre_mem_read          = NULL,
   .pre_mem_read_asciiz   = NULL,
   .pre_mem_write         = NULL,
   .post_mem_write        = NULL,

   /* Mutex events */
   .post_mutex_lock       = NULL,
   .post_mutex_unlock     = NULL,
};

static void sanity_check_needs ( void )
{
#define CHECK_NOT(var, value)                               \
   if ((var)==(value)) {                                    \
      VG_(printf)("\nSkin error: `%s' not initialised\n",   \
                  VG__STRING(var));                         \
      VG_(skin_panic)("Uninitialised needs field\n");       \
   }
   
   CHECK_NOT(VG_(needs).name,           NULL);
   CHECK_NOT(VG_(needs).description,    NULL);
   CHECK_NOT(VG_(needs).bug_reports_to, NULL);

#undef CHECK_NOT
#undef INVALID_Bool
}

/* ---------------------------------------------------------------------
   Values derived from command-line options.
   ------------------------------------------------------------------ */

/* Define, and set defaults. */
Bool   VG_(clo_error_limit)    = True;
Bool   VG_(clo_GDB_attach)     = False;
Int    VG_(sanity_level)       = 1;
Int    VG_(clo_verbosity)      = 1;
Bool   VG_(clo_demangle)       = True;
Bool   VG_(clo_sloppy_malloc)  = False;
Int    VG_(clo_alignment)      = 4;
Bool   VG_(clo_trace_children) = False;
Int    VG_(clo_logfile_fd)     = 2;
Int    VG_(clo_n_suppressions) = 0;
Char*  VG_(clo_suppressions)[VG_CLO_MAX_SFILES];
Bool   VG_(clo_profile)        = False;
Bool   VG_(clo_single_step)    = False;
Bool   VG_(clo_optimise)       = True;
UChar  VG_(clo_trace_codegen)  = 0; // 00000000b
Bool   VG_(clo_trace_syscalls) = False;
Bool   VG_(clo_trace_signals)  = False;
Bool   VG_(clo_trace_symtab)   = False;
Bool   VG_(clo_trace_malloc)   = False;
Bool   VG_(clo_trace_sched)    = False;
Int    VG_(clo_trace_pthread_level) = 0;
ULong  VG_(clo_stop_after)     = 1000000000000LL;
Int    VG_(clo_dump_error)     = 0;
Int    VG_(clo_backtrace_size) = 4;
Char*  VG_(clo_weird_hacks)    = NULL;

/* This Bool is needed by wrappers in vg_clientmalloc.c to decide how
   to behave.  Initially we say False. */
Bool VG_(running_on_simd_CPU) = False;

/* Holds client's %esp at the point we gained control. */
Addr VG_(esp_at_startup);

/* As deduced from VG_(esp_at_startup), the client's argc, argv[] and
   envp[] as extracted from the client's stack at startup-time. */
Int    VG_(client_argc);
Char** VG_(client_argv);
Char** VG_(client_envp);

/* A place into which to copy the value of env var VG_ARGS, so we
   don't have to modify the original. */
static Char vg_cmdline_copy[M_VG_CMDLINE_STRLEN];

/* ---------------------------------------------------------------------
   Processing of command-line options.
   ------------------------------------------------------------------ */

void VG_(bad_option) ( Char* opt )
{
   VG_(shutdown_logging)();
   VG_(clo_logfile_fd) = 2; /* stderr */
   VG_(printf)("valgrind.so: Bad option `%s'; aborting.\n", opt);
   VG_(exit)(1);
}

static void config_error ( Char* msg )
{
   VG_(shutdown_logging)();
   VG_(clo_logfile_fd) = 2; /* stderr */
   VG_(printf)(
      "valgrind.so: Startup or configuration error:\n   %s\n", msg);
   VG_(printf)(
      "valgrind.so: Unable to start up properly.  Giving up.\n");
   VG_(exit)(1);
}

static void args_grok_error ( Char* msg )
{
   VG_(shutdown_logging)();
   VG_(clo_logfile_fd) = 2; /* stderr */
   VG_(printf)("valgrind.so: When searching for "
               "client's argc/argc/envp:\n\t%s\n", msg);
   config_error("couldn't find client's argc/argc/envp");
}   

static void usage ( void )
{
   Char* usage1 = 
"usage: valgrind [options] prog-and-args\n"
"\n"
"  core user options, with defaults in [ ], are:\n"
"    --help                    show this message\n"
"    --version                 show version\n"
"    --skin=<name>             main task (skin to use) [Valgrind]\n"
"    -q --quiet                run silently; only print error msgs\n"
"    -v --verbose              be more verbose, incl counts of errors\n"
"    --gdb-attach=no|yes       start GDB when errors detected? [no]\n"
"    --demangle=no|yes         automatically demangle C++ names? [yes]\n"
"    --num-callers=<number>    show <num> callers in stack traces [4]\n"
"    --error-limit=no|yes      stop showing new errors if too many? [yes]\n"
"    --sloppy-malloc=no|yes    round malloc sizes to next word? [no]\n"
"    --alignment=<number>      set minimum alignment of allocations [4]\n"
"    --trace-children=no|yes   Valgrind-ise child processes? [no]\n"
"    --logfile-fd=<number>     file descriptor for messages [2=stderr]\n"
"    --suppressions=<filename> suppress errors described in\n"
"                              suppressions file <filename>\n"
"    --weird-hacks=hack1,hack2,...  [no hacks selected]\n"
"         recognised hacks are: ioctl-VTIME truncate-writes\n"
"\n"
"  %s skin user options:\n";


   Char* usage2 = 
"\n"
"  core options for debugging Valgrind itself are:\n"
"    --sanity-level=<number>   level of sanity checking to do [1]\n"
"    --single-step=no|yes      translate each instr separately? [no]\n"
"    --optimise=no|yes         improve intermediate code? [yes]\n"
"    --profile=no|yes          profile? (skin must be built for it) [no]\n"
"    --trace-codegen=<XXXXX>   show generated code? (X = 0|1) [00000]\n"
"    --trace-syscalls=no|yes   show all system calls? [no]\n"
"    --trace-signals=no|yes    show signal handling details? [no]\n"
"    --trace-symtab=no|yes     show symbol table details? [no]\n"
"    --trace-malloc=no|yes     show client malloc details? [no]\n"
"    --trace-sched=no|yes      show thread scheduler details? [no]\n"
"    --trace-pthread=none|some|all  show pthread event details? [no]\n"
"    --stop-after=<number>     switch to real CPU after executing\n"
"                              <number> basic blocks [infinity]\n"
"    --dump-error=<number>     show translation for basic block\n"
"                              associated with <number>'th\n"
"                              error context [0=don't show any]\n"
"\n"
"  Extra options are read from env variable $VALGRIND_OPTS\n"
"\n"
"  Valgrind is Copyright (C) 2000-2002 Julian Seward\n"
"  and licensed under the GNU General Public License, version 2.\n"
"  Bug reports, feedback, admiration, abuse, etc, to: %s.\n"
"\n";

   VG_(printf)(usage1, VG_(needs).name);
   /* Don't print skin string directly for security, ha! */
   if (VG_(needs).command_line_options)
      VG_(printf)("%s", SK_(usage)());
   else
      VG_(printf)("    (none)\n");
   VG_(printf)(usage2, VG_EMAIL_ADDR);

   VG_(shutdown_logging)();
   VG_(clo_logfile_fd) = 2; /* stderr */
   VG_(exit)(1);
}

static void process_cmd_line_options ( void )
{
   Char* argv[M_VG_CMDLINE_OPTS];
   UInt  argc;
   Char* p;
   Char* str;
   Int   i, eventually_logfile_fd, ctr;

#  define ISSPACE(cc)      ((cc) == ' ' || (cc) == '\t' || (cc) == '\n')
#  define STREQ(s1,s2)     (0==VG_(strcmp_ws)((s1),(s2)))
#  define STREQN(nn,s1,s2) (0==VG_(strncmp_ws)((s1),(s2),(nn)))

   eventually_logfile_fd = VG_(clo_logfile_fd);

   /* Once logging is started, we can safely send messages pertaining
      to failures in initialisation. */
   VG_(startup_logging)();

   /* Check for sane path in ./configure --prefix=... */
   if (VG_(strlen)(VG_LIBDIR) < 1 
       || VG_LIBDIR[0] != '/') 
     config_error("Please use absolute paths in "
                  "./configure --prefix=... or --libdir=...");

   /* (Suggested by Fabrice Bellard ... )
      We look for the Linux ELF table and go down until we find the
      envc & envp. It is not fool-proof, but these structures should
      change less often than the libc ones. */
   {
       UInt* sp = 0; /* bogus init to keep gcc -O happy */

       /* locate the top of the stack */
       if (VG_STACK_MATCHES_BASE( VG_(esp_at_startup), 
                                  VG_STARTUP_STACK_BASE_1 )) {
          sp = (UInt*)VG_STARTUP_STACK_BASE_1;
       } else
       if (VG_STACK_MATCHES_BASE( VG_(esp_at_startup), 
                                  VG_STARTUP_STACK_BASE_2 )) {
          sp = (UInt*)VG_STARTUP_STACK_BASE_2;
       } else 
       if (VG_STACK_MATCHES_BASE( VG_(esp_at_startup), 
                                  VG_STARTUP_STACK_BASE_3 )) {
          sp = (UInt*)VG_STARTUP_STACK_BASE_3;
       } else 
       if (VG_STACK_MATCHES_BASE( VG_(esp_at_startup), 
                                  VG_STARTUP_STACK_BASE_4 )) {
          sp = (UInt*)VG_STARTUP_STACK_BASE_4;
       } else {
          args_grok_error(
             "startup %esp is not near any VG_STARTUP_STACK_BASE_*\n   "
             "constants defined in vg_include.h.  You should investigate."
          );
       }
 
       /* we locate: NEW_AUX_ENT(1, AT_PAGESZ, ELF_EXEC_PAGESIZE) in
          the elf interpreter table */
       sp -= 2;
       while (sp[0] != VKI_AT_PAGESZ || sp[1] != 4096) {
           /* VG_(printf)("trying %p\n", sp); */
           sp--;
       }

       if (sp[2] == VKI_AT_BASE 
           && sp[0] == VKI_AT_PAGESZ
           && sp[-2] == VKI_AT_PHNUM
           && sp[-4] == VKI_AT_PHENT
           && sp[-6] == VKI_AT_PHDR
           && sp[-6-1] == 0) {
          if (0)
             VG_(printf)("Looks like you've got a 2.2.X kernel here.\n");
          sp -= 6;
       } else
       if (sp[2] == VKI_AT_CLKTCK
           && sp[0] == VKI_AT_PAGESZ
           && sp[-2] == VKI_AT_HWCAP
           && sp[-2-1] == 0) {
          if (0)
             VG_(printf)("Looks like you've got a 2.4.X kernel here.\n");
          sp -= 2;
       } else
       if (sp[2] == VKI_AT_CLKTCK
           && sp[0] == VKI_AT_PAGESZ
           && sp[-2] == VKI_AT_HWCAP
           && sp[-4] == VKI_AT_USER_AUX_SEGMENT
           && sp[-4-1] == 0) {
          if (0)
             VG_(printf)("Looks like you've got a R-H Limbo 2.4.X "
                         "kernel here.\n");
          sp -= 4;
       } else
       if (sp[2] == VKI_AT_CLKTCK
           && sp[0] == VKI_AT_PAGESZ
           && sp[-2] == VKI_AT_HWCAP
           && sp[-2-20-1] == 0) {
          if (0)
             VG_(printf)("Looks like you've got a early 2.4.X kernel here.\n");
          sp -= 22;
       } else
         args_grok_error(
            "ELF frame does not look like 2.2.X or 2.4.X.\n   "
            "See kernel sources linux/fs/binfmt_elf.c to make sense of this."
         );

       sp--;
       if (*sp != 0)
	 args_grok_error("can't find NULL at end of env[]");

       /* sp now points to NULL at the end of env[] */
       ctr = 0;
       while (True) {
           sp --;
           if (*sp == 0) break;
           if (++ctr >= 1000)
              args_grok_error(
                 "suspiciously many (1000) env[] entries; giving up");
           
       }
       /* sp now points to NULL at the end of argv[] */
       VG_(client_envp) = (Char**)(sp+1);

       ctr = 0;
       VG_(client_argc) = 0;
       while (True) {
          sp--;
          if (*sp == VG_(client_argc))
             break;
          VG_(client_argc)++;
           if (++ctr >= 1000)
              args_grok_error(
                 "suspiciously many (1000) argv[] entries; giving up");
       }

       VG_(client_argv) = (Char**)(sp+1);
   }

   /* Now that VG_(client_envp) has been set, we can extract the args
      for Valgrind itself.  Copy into global var so that we don't have to
      write zeroes to the getenv'd value itself. */
   str = VG_(getenv)("VG_ARGS");
   argc = 0;

   if (!str) {
      config_error("Can't read options from env var VG_ARGS.");
   }

   if (VG_(strlen)(str) >= M_VG_CMDLINE_STRLEN-1) {
      config_error("Command line length exceeds M_CMDLINE_STRLEN.");
   }
   VG_(strcpy)(vg_cmdline_copy, str);
   str = NULL;

   p = &vg_cmdline_copy[0];
   while (True) {
      while (ISSPACE(*p)) { *p = 0; p++; }
      if (*p == 0) break;
      if (argc < M_VG_CMDLINE_OPTS-1) { 
         argv[argc] = p; argc++; 
      } else {
         config_error(
            "Found more than M_CMDLINE_OPTS command-line opts.");
      }
      while (*p != 0 && !ISSPACE(*p)) p++;
   }

   for (i = 0; i < argc; i++) {

      if      (STREQ(argv[i], "-v") || STREQ(argv[i], "--verbose"))
         VG_(clo_verbosity)++;
      else if (STREQ(argv[i], "-q") || STREQ(argv[i], "--quiet"))
         VG_(clo_verbosity)--;

      else if (STREQ(argv[i], "--error-limit=yes"))
         VG_(clo_error_limit) = True;
      else if (STREQ(argv[i], "--error-limit=no"))
         VG_(clo_error_limit) = False;

      else if (STREQ(argv[i], "--gdb-attach=yes"))
         VG_(clo_GDB_attach) = True;
      else if (STREQ(argv[i], "--gdb-attach=no"))
         VG_(clo_GDB_attach) = False;

      else if (STREQ(argv[i], "--demangle=yes"))
         VG_(clo_demangle) = True;
      else if (STREQ(argv[i], "--demangle=no"))
         VG_(clo_demangle) = False;

      else if (STREQ(argv[i], "--sloppy-malloc=yes"))
         VG_(clo_sloppy_malloc) = True;
      else if (STREQ(argv[i], "--sloppy-malloc=no"))
         VG_(clo_sloppy_malloc) = False;

      else if (STREQN(12, argv[i], "--alignment="))
         VG_(clo_alignment) = (Int)VG_(atoll)(&argv[i][12]);

      else if (STREQ(argv[i], "--trace-children=yes"))
         VG_(clo_trace_children) = True;
      else if (STREQ(argv[i], "--trace-children=no"))
         VG_(clo_trace_children) = False;

      else if (STREQN(15, argv[i], "--sanity-level="))
         VG_(sanity_level) = (Int)VG_(atoll)(&argv[i][15]);

      else if (STREQN(13, argv[i], "--logfile-fd="))
         eventually_logfile_fd = (Int)VG_(atoll)(&argv[i][13]);

      else if (STREQN(15, argv[i], "--suppressions=")) {
         if (VG_(clo_n_suppressions) >= VG_CLO_MAX_SFILES) {
            VG_(message)(Vg_UserMsg, "Too many suppression files specified.");
            VG_(message)(Vg_UserMsg, 
                         "Increase VG_CLO_MAX_SFILES and recompile.");
            VG_(bad_option)(argv[i]);
         }
         VG_(clo_suppressions)[VG_(clo_n_suppressions)] = &argv[i][15];
         VG_(clo_n_suppressions)++;
      }
      else if (STREQ(argv[i], "--profile=yes"))
         VG_(clo_profile) = True;
      else if (STREQ(argv[i], "--profile=no"))
         VG_(clo_profile) = False;

      else if (STREQ(argv[i], "--single-step=yes"))
         VG_(clo_single_step) = True;
      else if (STREQ(argv[i], "--single-step=no"))
         VG_(clo_single_step) = False;

      else if (STREQ(argv[i], "--optimise=yes"))
         VG_(clo_optimise) = True;
      else if (STREQ(argv[i], "--optimise=no"))
         VG_(clo_optimise) = False;

      /* "vwxyz" --> 000zyxwv (binary) */
      else if (STREQN(16, argv[i], "--trace-codegen=")) {
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

      else if (STREQ(argv[i], "--trace-syscalls=yes"))
         VG_(clo_trace_syscalls) = True;
      else if (STREQ(argv[i], "--trace-syscalls=no"))
         VG_(clo_trace_syscalls) = False;

      else if (STREQ(argv[i], "--trace-signals=yes"))
         VG_(clo_trace_signals) = True;
      else if (STREQ(argv[i], "--trace-signals=no"))
         VG_(clo_trace_signals) = False;

      else if (STREQ(argv[i], "--trace-symtab=yes"))
         VG_(clo_trace_symtab) = True;
      else if (STREQ(argv[i], "--trace-symtab=no"))
         VG_(clo_trace_symtab) = False;

      else if (STREQ(argv[i], "--trace-malloc=yes"))
         VG_(clo_trace_malloc) = True;
      else if (STREQ(argv[i], "--trace-malloc=no"))
         VG_(clo_trace_malloc) = False;

      else if (STREQ(argv[i], "--trace-sched=yes"))
         VG_(clo_trace_sched) = True;
      else if (STREQ(argv[i], "--trace-sched=no"))
         VG_(clo_trace_sched) = False;

      else if (STREQ(argv[i], "--trace-pthread=none"))
         VG_(clo_trace_pthread_level) = 0;
      else if (STREQ(argv[i], "--trace-pthread=some"))
         VG_(clo_trace_pthread_level) = 1;
      else if (STREQ(argv[i], "--trace-pthread=all"))
         VG_(clo_trace_pthread_level) = 2;

      else if (STREQN(14, argv[i], "--weird-hacks="))
         VG_(clo_weird_hacks) = &argv[i][14];

      else if (STREQN(13, argv[i], "--stop-after="))
         VG_(clo_stop_after) = VG_(atoll)(&argv[i][13]);

      else if (STREQN(13, argv[i], "--dump-error="))
         VG_(clo_dump_error) = (Int)VG_(atoll)(&argv[i][13]);

      else if (STREQN(14, argv[i], "--num-callers=")) {
         /* Make sure it's sane. */
	 VG_(clo_backtrace_size) = (Int)VG_(atoll)(&argv[i][14]);
         if (VG_(clo_backtrace_size) < 2)
            VG_(clo_backtrace_size) = 2;
         if (VG_(clo_backtrace_size) >= VG_DEEPEST_BACKTRACE)
            VG_(clo_backtrace_size) = VG_DEEPEST_BACKTRACE;
      }

      else if (VG_(needs).command_line_options) {
         Bool ok = SK_(process_cmd_line_option)(argv[i]);
         if (!ok)
            usage();
      }
      else
         usage();
   }

#  undef ISSPACE
#  undef STREQ
#  undef STREQN

   if (VG_(clo_verbosity < 0))
      VG_(clo_verbosity) = 0;

   if (VG_(clo_alignment) < 4 
       || VG_(clo_alignment) > 4096
       || VG_(log2)( VG_(clo_alignment) ) == -1 /* not a power of 2 */) {
      VG_(message)(Vg_UserMsg, "");
      VG_(message)(Vg_UserMsg, 
         "Invalid --alignment= setting.  "
         "Should be a power of 2, >= 4, <= 4096.");
      VG_(bad_option)("--alignment");
   }

   if (VG_(clo_GDB_attach) && VG_(clo_trace_children)) {
      VG_(message)(Vg_UserMsg, "");
      VG_(message)(Vg_UserMsg, 
         "--gdb-attach=yes conflicts with --trace-children=yes");
      VG_(message)(Vg_UserMsg, 
         "Please choose one or the other, but not both.");
      VG_(bad_option)("--gdb-attach=yes and --trace-children=yes");
   }

   VG_(clo_logfile_fd) = eventually_logfile_fd;

   if (VG_(clo_verbosity > 0)) {
      VG_(message)(Vg_UserMsg, "%s-%s, %s for x86 GNU/Linux.",
         VG_(needs).name, VERSION, VG_(needs).description);
   }

   if (VG_(clo_verbosity > 0))
      VG_(message)(Vg_UserMsg, 
                   "Copyright (C) 2000-2002, and GNU GPL'd, by Julian Seward.");
   if (VG_(clo_verbosity) > 1) {
      VG_(message)(Vg_UserMsg, "Startup, with flags:");
      for (i = 0; i < argc; i++) {
         VG_(message)(Vg_UserMsg, "   %s", argv[i]);
      }
   }

   if (VG_(clo_n_suppressions) == 0 && 
       (VG_(needs).core_errors || VG_(needs).skin_errors)) {
      config_error("No error-suppression files were specified.");
   }
}

/* ---------------------------------------------------------------------
   Copying to/from m_state_static.
   ------------------------------------------------------------------ */

UInt VG_(m_state_static) [6 /* segment regs, Intel order */
                          + 8 /* int regs, in Intel order */ 
                          + 1 /* %eflags */ 
                          + 1 /* %eip */
                          + VG_SIZE_OF_FPUSTATE_W /* FPU state */
                         ];

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

   VG_(m_state_static)[56/4] = VG_(baseBlock)[VGOFF_(m_eflags)];
   VG_(m_state_static)[60/4] = VG_(baseBlock)[VGOFF_(m_eip)];

   for (i = 0; i < VG_SIZE_OF_FPUSTATE_W; i++)
      VG_(m_state_static)[64/4 + i] 
         = VG_(baseBlock)[VGOFF_(m_fpustate) + i];
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

   VG_(baseBlock)[VGOFF_(m_eflags)] = VG_(m_state_static)[56/4];
   VG_(baseBlock)[VGOFF_(m_eip)] = VG_(m_state_static)[60/4];

   for (i = 0; i < VG_SIZE_OF_FPUSTATE_W; i++)
      VG_(baseBlock)[VGOFF_(m_fpustate) + i]
         = VG_(m_state_static)[64/4 + i];
}

Addr VG_(get_stack_pointer) ( void )
{
   return VG_(baseBlock)[VGOFF_(m_esp)];
}

/* Some random tests needed for leak checking */

Bool VG_(within_stack)(Addr a)
{
   if (a >= ((Addr)(&VG_(stack)))
       && a <= ((Addr)(&VG_(stack))) + sizeof(VG_(stack)))
      return True;
   else
      return False;
}

Bool VG_(within_m_state_static)(Addr a)
{
   if (a >= ((Addr)(&VG_(m_state_static)))
       && a <= ((Addr)(&VG_(m_state_static))) + sizeof(VG_(m_state_static)))
      return True;
   else
      return False;
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
		"      lru: %d epochs, %d clearings.",
		VG_(current_epoch),
                VG_(number_of_lrus) );
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
      " dispatch: %lu basic blocks, %d/%d sched events, %d tt_fast misses.", 
      VG_(bbs_done), VG_(num_scheduling_events_MAJOR), 
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

/* Where we jump to once Valgrind has got control, and the real
   machine's state has been copied to the m_state_static. */

void VG_(main) ( void )
{
   Int               i;
   VgSchedReturnCode src;
   ThreadState*      tst;

   /* Set up our stack sanity-check words. */
   for (i = 0; i < 10; i++) {
      VG_(stack)[i]         = (UInt)(&VG_(stack)[i])         ^ 0xA4B3C2D1;
      VG_(stack)[10000-1-i] = (UInt)(&VG_(stack)[10000-i-1]) ^ 0xABCD4321;
   }

   /* Setup stuff that depends on the skin.  Must be before:
      - vg_init_baseBlock(): to register helpers
      - process_cmd_line_options(): to register skin name and description,
        and turn on/off 'command_line_options' need
      - init_memory() (to setup memory event trackers).
    */
   SK_(pre_clo_init) ( & VG_(needs), & VG_(track_events) );
   sanity_check_needs();

   /* Set up baseBlock offsets and copy the saved machine's state into it. */
   vg_init_baseBlock();
   VG_(copy_m_state_static_to_baseBlock)();
   /* Pretend that the root thread has a completely empty LDT to start
      with. */
   VG_(baseBlock)[VGOFF_(ldt)] = (UInt)NULL;
   vg_init_shadow_regs();

   /* Process Valgrind's command-line opts (from env var VG_OPTS). */
   process_cmd_line_options();

   /* Hook to delay things long enough so we can get the pid and
      attach GDB in another shell. */
   if (0) { 
      Int p, q;
      for (p = 0; p < 50000; p++)
         for (q = 0; q < 50000; q++) ;
   }

   /* Initialise the scheduler, and copy the client's state from
      baseBlock into VG_(threads)[1].  This has to come before signal
      initialisations. */
   VG_(scheduler_init)();

   /* Initialise the signal handling subsystem, temporarily parking
      the saved blocking-mask in saved_sigmask. */
   VG_(sigstartup_actions)();

   /* Perhaps we're profiling Valgrind? */
   if (VG_(clo_profile))
      VGP_(init_profiling)();

   /* Start calibration of our RDTSC-based clock. */
   VG_(start_rdtsc_calibration)();

   /* Do this here just to give rdtsc calibration more time */
   SK_(post_clo_init)();

   /* Must come after SK_(init) so memory handler accompaniments (eg.
    * shadow memory) can be setup ok */
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

   /* This should come after init_memory_and_symbols(); otherwise the 
      latter carefully sets up the permissions maps to cover the 
      anonymous mmaps for the translation table and translation cache, 
      which wastes > 20M of virtual address space. */
   VG_(init_tt_tc)();

   if (VG_(clo_verbosity) == 1) {
      VG_(message)(Vg_UserMsg, 
                   "For more details, rerun with: -v");
   }

   /* Now it is safe for malloc et al in vg_clientmalloc.c to act
      instrumented-ly. */
   if (VG_(clo_verbosity) > 0)
      VG_(message)(Vg_UserMsg, "");

   VG_(bbs_to_go) = VG_(clo_stop_after);

   /* Run! */
   VG_(running_on_simd_CPU) = True;
   VGP_PUSHCC(VgpSched);
   src = VG_(scheduler)();
   VGP_POPCC(VgpSched);
   VG_(running_on_simd_CPU) = False;

   if (VG_(clo_verbosity) > 0)
      VG_(message)(Vg_UserMsg, "");

   if (src == VgSrc_Deadlock) {
     VG_(message)(Vg_UserMsg, 
        "Warning: pthread scheduler exited due to deadlock");
   }

   if (VG_(needs).core_errors || VG_(needs).skin_errors)
      VG_(show_all_errors)();

   SK_(fini)();

   VG_(do_sanity_checks)( True /*include expensive checks*/ );

   if (VG_(clo_verbosity) > 1)
      vg_show_counts();

   if (VG_(clo_verbosity) > 2)
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

   /* Remove valgrind.so from a LD_PRELOAD=... string so child
      processes don't get traced into.  Also mess up $libdir/valgrind
      so that our libpthread.so disappears from view. */
   if (!VG_(clo_trace_children)) { 
      VG_(mash_LD_PRELOAD_and_LD_LIBRARY_PATH)(
         VG_(getenv)("LD_PRELOAD"),
         VG_(getenv)("LD_LIBRARY_PATH") 
      );
   }

   /* Decide how to exit.  This depends on what the scheduler
      returned. */
   switch (src) {
      case VgSrc_ExitSyscall: /* the normal way out */
         vg_assert(VG_(last_run_tid) > 0 
                   && VG_(last_run_tid) < VG_N_THREADS);
         tst = & VG_(threads)[VG_(last_run_tid)];
         vg_assert(tst->status == VgTs_Runnable);
         /* The thread's %EBX at the time it did __NR_exit() will hold
            the arg to __NR_exit(), so we just do __NR_exit() with
            that arg. */
         VG_(exit)( VG_(exitcode) );
         /* NOT ALIVE HERE! */
         VG_(core_panic)("entered the afterlife in vg_main() -- ExitSyscall");
         break; /* what the hell :) */

      case VgSrc_Deadlock:
         /* Just exit now.  No point in continuing. */
         VG_(exit)(0);
         VG_(core_panic)("entered the afterlife in vg_main() -- Deadlock");
         break;

      case VgSrc_BbsDone: 
         /* Tricky; we have to try and switch back to the real CPU.
            This is all very dodgy and won't work at all in the
            presence of threads, or if the client happened to be
            running a signal handler. */
         /* Prepare to restore state to the real CPU. */
         VG_(load_thread_state)(1 /* root thread */ );
         VG_(copy_baseBlock_to_m_state_static)();

         /* This pushes a return address on the simulator's stack,
            which is abandoned.  We call vg_sigshutdown_actions() at
            the end of vg_switch_to_real_CPU(), so as to ensure that
            the original stack and machine state is restored before
            the real signal mechanism is restored.  */
         VG_(switch_to_real_CPU)();

      default:
         VG_(core_panic)("vg_main(): unexpected scheduler return code");
   }
}


/* Debugging thing .. can be called from assembly with OYNK macro. */
void VG_(oynk) ( Int n )
{
   OINK(n);
}


/* Find "valgrind.so" in a LD_PRELOAD=... string, and convert it to
   "valgrinq.so", which doesn't do anything.  This is used to avoid
   tracing into child processes.  To make this work the build system
   also supplies a dummy file, "valgrinq.so". 

   Also replace "vgskin_<foo>.so" with whitespace, for the same reason;
   without it, child processes try to find valgrind.so symbols in the 
   skin .so.

   Also look for $(libdir)/lib/valgrind in LD_LIBRARY_PATH and change
   it to $(libdir)/lib/valgrinq, so as to make our libpthread.so
   disappear.  
*/
void VG_(mash_LD_PRELOAD_and_LD_LIBRARY_PATH) ( Char* ld_preload_str,
                                                Char* ld_library_path_str )
{
   Char* p_prel  = NULL;
   Char* sk_prel = NULL;
   Char* p_path  = NULL;
   Int   what    = 0;
   if (ld_preload_str == NULL || ld_library_path_str == NULL)
      goto mutancy;

   /* VG_(printf)("%s %s\n", ld_preload_str, ld_library_path_str); */

   p_prel = VG_(strstr)(ld_preload_str, "valgrind.so");
   sk_prel = VG_(strstr)(ld_preload_str, "vgskin_");
   p_path = VG_(strstr)(ld_library_path_str, VG_LIBDIR);

   what = 1;
   if (p_prel == NULL) {
      /* perhaps already happened? */
      if (VG_(strstr)(ld_preload_str, "valgrinq.so") == NULL)
         goto mutancy;
      if (VG_(strstr)(ld_library_path_str, "lib/valgrinq") == NULL)
         goto mutancy;
      return;
   }

   what = 2;
   if (sk_prel == NULL) goto mutancy;

   what = 3;
   if (p_path == NULL) goto mutancy;

   what = 4;
   {  
      /* Blank from "vgskin_" back to prev. LD_PRELOAD entry, or start */
      Char* p = sk_prel;
      while (*p != ':' && p > ld_preload_str) { 
         *p = ' ';
         p--;
      }
      /* Blank from "vgskin_" to next LD_PRELOAD entry */
      while (*p != ':' && *p != '\0') { 
         *p = ' ';
         p++;
      }
      if (*p == '\0') goto mutancy;    /* valgrind.so has disappeared?! */
      *p = ' ';                        /* blank ending ':' */
   }

   /* in LD_PRELOAD, turn valgrind.so into valgrinq.so. */
   what = 5;
   if (p_prel[7] != 'd') goto mutancy;
   p_prel[7] = 'q';

   /* in LD_LIBRARY_PATH, turn $libdir/valgrind (as configure'd) from 
      .../lib/valgrind .../lib/valgrinq, which doesn't exist,
      so that our own libpthread.so goes out of scope. */
   p_path += VG_(strlen)(VG_LIBDIR);
   what = 6;
   if (p_path[0] != '/') goto mutancy;
   p_path++; /* step over / */
   what = 7;
   if (p_path[7] != 'd') goto mutancy;
   p_path[7] = 'q';
   return;

  mutancy:
   VG_(printf)(
      "\nVG_(mash_LD_PRELOAD_and_LD_LIBRARY_PATH): internal error:\n"
      "   what                = %d\n"
      "   ld_preload_str      = `%s'\n"
      "   ld_library_path_str = `%s'\n"
      "   p_prel              = `%s'\n"
      "   p_path              = `%s'\n"
      "   VG_LIBDIR           = `%s'\n",
      what, ld_preload_str, ld_library_path_str, 
      p_prel, p_path, VG_LIBDIR 
   );
   VG_(printf)(
      "\n"
      "Note that this is often caused by mis-installation of valgrind.\n"
      "Correct installation procedure is:\n"
      "   ./configure --prefix=/install/dir\n"
      "   make install\n"
      "And then use /install/dir/bin/valgrind\n"
      "Moving the installation directory elsewhere after 'make install'\n"
      "will cause the above error.  Hand-editing the paths in the shell\n"
      "scripts is also likely to cause problems.\n"
      "\n"
   );
   VG_(core_panic)("VG_(mash_LD_PRELOAD_and_LD_LIBRARY_PATH) failed\n");
}


/* RUNS ON THE CLIENT'S STACK, but on the real CPU.  Start GDB and get
   it to attach to this process.  Called if the user requests this
   service after an error has been shown, so she can poke around and
   look at parameters, memory, etc.  You can't meaningfully get GDB to
   continue the program, though; to continue, quit GDB.  */
extern void VG_(start_GDB_whilst_on_client_stack) ( void )
{
   Int   res;
   UChar buf[100];
   VG_(sprintf)(buf,
                "/usr/bin/gdb -nw /proc/%d/exe %d", 
                VG_(getpid)(), VG_(getpid)());
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
      "or because nobody has yet needed it.  In any case, let me know");
   VG_(message)(Vg_UserMsg,
      "(jseward@acm.org) and/or try to work around the problem, if you can.");
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
   Int          i;

   VGP_PUSHCC(VgpCoreCheapSanity);

   if (VG_(sanity_level) < 1) return;

   /* --- First do all the tests that we can do quickly. ---*/

   VG_(sanity_fast_count)++;

   /* Check that we haven't overrun our private stack. */
   for (i = 0; i < 10; i++) {
      vg_assert(VG_(stack)[i]
                == ((UInt)(&VG_(stack)[i]) ^ 0xA4B3C2D1));
      vg_assert(VG_(stack)[10000-1-i] 
                == ((UInt)(&VG_(stack)[10000-i-1]) ^ 0xABCD4321));
   }

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

/*--------------------------------------------------------------------*/
/*--- Default panicky definitions of template functions that skins ---*/
/*--- should override.                                             ---*/
/*---                                                vg_defaults.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, an x86 protected-mode emulator 
   designed for debugging and profiling binaries on x86-Unixes.

   Copyright (C) 2000-2002 Nicholas Nethercote
      njn25@cam.ac.uk

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


/* These functions aren't intended to be run.  Replacement functions used by
 * the chosen skin are substituted by compiling the skin into a .so and
 * LD_PRELOADing it.  Nasty :) */

#include "vg_include.h"

/* ---------------------------------------------------------------------
   Error messages (for malformed skins)
   ------------------------------------------------------------------ */

/* If the skin fails to define one or more of the required functions,
 * make it very clear what went wrong! */

static __attribute__ ((noreturn))
void fund_panic ( Char* fn )
{
   VG_(printf)(
      "\nSkin error:\n"
      "  The skin you have selected is missing the function `%s',\n"
      "  which is required.\n\n",
      fn);
   VG_(skin_panic)("Missing skin function");
}

static __attribute__ ((noreturn))
void non_fund_panic ( Char* fn )
{
   VG_(printf)(
      "\nSkin error:\n"
      "  The skin you have selected is missing the function `%s'\n"
      "  required by one of its needs.\n\n",
      fn);
   VG_(skin_panic)("Missing skin function");
}

/* ---------------------------------------------------------------------
   Fundamental template functions
   ------------------------------------------------------------------ */

__attribute__ ((weak))
void SK_(pre_clo_init)(VgNeeds* needs, VgTrackEvents* track)
{
   fund_panic("SK_(pre_clo_init)");
}

__attribute__ ((weak))
void SK_(post_clo_init)(void)
{
   fund_panic("SK_(post_clo_init)");
}

__attribute__ ((weak))
UCodeBlock* SK_(instrument)(UCodeBlock* cb, Addr not_used)
{
   fund_panic("SK_(instrument)");
}

__attribute__ ((weak))
void SK_(fini)(void)
{
   fund_panic("SK_(fini)");
}

/* ---------------------------------------------------------------------
   For error reporting and suppression handling
   ------------------------------------------------------------------ */

__attribute__ ((weak))
Bool SK_(eq_SkinError)(VgRes res, SkinError* e1, SkinError* e2)
{
   non_fund_panic("SK_(eq_SkinError)");
}

__attribute__ ((weak))
void SK_(pp_SkinError)(SkinError* ec, void (*pp_ExeContext)(void))
{
   non_fund_panic("SK_(pp_SkinError)");
}

__attribute__ ((weak))
void SK_(dup_extra_and_update)(SkinError* ec)
{
   non_fund_panic("SK_(dup_extra_and_update)");
}

__attribute__ ((weak))
Bool SK_(recognised_suppression)(Char* name, SuppKind* skind)
{
   non_fund_panic("SK_(recognised_suppression)");
}

__attribute__ ((weak))
Bool SK_(read_extra_suppression_info)(Int fd, Char* buf, 
                                       Int nBuf, SkinSupp *s)
{
   non_fund_panic("SK_(read_extra_suppression_info)");
}

__attribute__ ((weak))
Bool SK_(error_matches_suppression)(SkinError* ec, SkinSupp* su)
{
   non_fund_panic("SK_(error_matches_suppression)");
}


/* ---------------------------------------------------------------------
   For throwing out basic block level info when code is invalidated
   ------------------------------------------------------------------ */

__attribute__ ((weak))
void SK_(discard_basic_block_info)(Addr a, UInt size)
{
   non_fund_panic("SK_(discard_basic_block_info)");
}


/* ---------------------------------------------------------------------
   For throwing out basic block level info when code is invalidated
   ------------------------------------------------------------------ */

__attribute__ ((weak))
void SK_(written_shadow_regs_values)(UInt* gen_reg, UInt* eflags)
{
   non_fund_panic("SK_(written_shadow_regs_values)");
}


/* ---------------------------------------------------------------------
   Command line arg template function
   ------------------------------------------------------------------ */

__attribute__ ((weak))
Bool SK_(process_cmd_line_option)(Char* argv)
{
   non_fund_panic("SK_(process_cmd_line_option)");
}

__attribute__ ((weak))
Char* SK_(usage)(void)
{
   non_fund_panic("SK_(usage)");
}

/* ---------------------------------------------------------------------
   Client request template function
   ------------------------------------------------------------------ */

__attribute__ ((weak))
UInt SK_(handle_client_request)(ThreadState* tst, UInt* arg_block)
{
   non_fund_panic("SK_(handle_client_request)");
}

/* ---------------------------------------------------------------------
   UCode extension
   ------------------------------------------------------------------ */

__attribute__ ((weak))
void SK_(emit_XUInstr)(UInstr* u, RRegSet regs_live_before)
{
   non_fund_panic("SK_(emit_XUInstr)");
}

__attribute__ ((weak))
Bool SK_(sane_XUInstr)(Bool beforeRA, Bool beforeLiveness, UInstr* u)
{
   non_fund_panic("SK_(sane_XUInstr)");
}

__attribute__ ((weak))
Char* SK_(name_XUOpcode)(Opcode opc)
{
   non_fund_panic("SK_(name_XUOpcode)");
}

__attribute__ ((weak))
void SK_(pp_XUInstr)(UInstr* u)
{
   non_fund_panic("SK_(pp_XUInstr)");
}

__attribute__ ((weak))
Int SK_(get_Xreg_usage)(UInstr* u, Tag tag, RegUse* arr)
{
   non_fund_panic("SK_(get_Xreg_usage)");
}

/* ---------------------------------------------------------------------
   Syscall wrapping
   ------------------------------------------------------------------ */

__attribute__ ((weak))
void* SK_(pre_syscall)(ThreadId tid, UInt syscallno, Bool is_blocking)
{
   non_fund_panic("SK_(pre_syscall)");
}

__attribute__ ((weak))
void  SK_(post_syscall)(ThreadId tid, UInt syscallno,
                         void* pre_result, Int res, Bool is_blocking)
{
   non_fund_panic("SK_(post_syscall)");
}

/* ---------------------------------------------------------------------
   Shadow chunks
   ------------------------------------------------------------------ */

__attribute__ ((weak))
void SK_(complete_shadow_chunk)( ShadowChunk* sc, ThreadState* tst )
{
   non_fund_panic("SK_(complete_shadow_chunk)");
}

/* ---------------------------------------------------------------------
   Alternative free()
   ------------------------------------------------------------------ */

__attribute__ ((weak))
void SK_(alt_free) ( ShadowChunk* sc, ThreadState* tst )
{
   non_fund_panic("SK_(alt_free)");
}

/* ---------------------------------------------------------------------
   Sanity checks
   ------------------------------------------------------------------ */

__attribute__ ((weak))
Bool SK_(cheap_sanity_check)(void)
{
   non_fund_panic("SK_(cheap_sanity_check)");
}

__attribute__ ((weak))
Bool SK_(expensive_sanity_check)(void)
{
   non_fund_panic("SK_(expensive_sanity_check)");
}

/*--------------------------------------------------------------------*/
/*--- end                                            vg_defaults.c ---*/
/*--------------------------------------------------------------------*/


/*--------------------------------------------------------------------*/
/*--- A header file containing constants (for assembly code).      ---*/
/*---                                               vg_constants.h ---*/
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

#ifndef __VG_CONSTANTS_H
#define __VG_CONSTANTS_H

#include "vg_constants_skin.h"

/* This file is included in all Valgrind source files, including
   assembly ones. */

/* Magic values that %ebp might be set to when returning to the
   dispatcher.  The only other legitimate value is to point to the
   start of VG_(baseBlock).  These also are return values from
   VG_(run_innerloop) to the scheduler.

   EBP means %ebp can legitimately have this value when a basic block
   returns to the dispatch loop.  TRC means that this value is a valid
   thread return code, which the dispatch loop may return to the
   scheduler.  */
#define VG_TRC_EBP_JMP_SYSCALL    19 /* EBP and TRC */
#define VG_TRC_EBP_JMP_CLIENTREQ  23 /* EBP and TRC */
#define VG_TRC_EBP_JMP_YIELD      27 /* EBP and TRC */

#define VG_TRC_INNER_FASTMISS     31 /* TRC only; means fast-cache miss. */
#define VG_TRC_INNER_COUNTERZERO  29 /* TRC only; means bb ctr == 0 */
#define VG_TRC_UNRESUMABLE_SIGNAL 37 /* TRC only; got sigsegv/sigbus */

/* size of call instruction put into generated code at jump sites */
#define VG_PATCHME_CALLSZ	5

/* size of jmp instruction which overwrites the call */
#define VG_PATCHME_JMPSZ	5

/* maximum number of normal jumps which can appear in a basic block */
#define VG_MAX_JUMPS		2

/* Offset of code in a TCEntry */
#define VG_CODE_OFFSET		(8 + VG_MAX_JUMPS * 2)

/* Client address space segment limit descriptor entry */
#define VG_POINTERCHECK_SEGIDX	1

/* Debugging hack for assembly code ... sigh. */
#if 0
#define OYNK(nnn) pushal;  pushl $nnn; call VG_(oynk) ; addl $4,%esp; popal
#else
#define OYNK(nnn)
#endif

#if 0
#define OYNNK(nnn) pushal;  pushl $nnn; call VG_(oynk) ; addl $4,%esp; popal
#else
#define OYNNK(nnn)
#endif


/* Constants for the fast translation lookup cache. */
#define VG_TT_FAST_BITS 15
#define VG_TT_FAST_SIZE (1 << VG_TT_FAST_BITS)
#define VG_TT_FAST_MASK ((VG_TT_FAST_SIZE) - 1)

/* Constants for the fast original-code-write check cache. */


/* Assembly code stubs make this request */
#define VG_USERREQ__SIGNAL_RETURNS          0x4001

/* CPU features */
#define VG_X86_FEAT_FPU		(0*32 + 0)
#define VG_X86_FEAT_VME		(0*32 + 1)
#define VG_X86_FEAT_DE		(0*32 + 2)
#define VG_X86_FEAT_PSE		(0*32 + 3)
#define VG_X86_FEAT_TSC		(0*32 + 4)
#define VG_X86_FEAT_MSR		(0*32 + 5)
#define VG_X86_FEAT_PAE		(0*32 + 6)
#define VG_X86_FEAT_MCE		(0*32 + 7)
#define VG_X86_FEAT_CX8		(0*32 + 8)
#define VG_X86_FEAT_APIC	(0*32 + 9)
#define VG_X86_FEAT_SEP		(0*32 + 11)
#define VG_X86_FEAT_MTRR	(0*32 + 12)
#define VG_X86_FEAT_PGE		(0*32 + 13)
#define VG_X86_FEAT_MCA		(0*32 + 14)
#define VG_X86_FEAT_CMOV	(0*32 + 15)
#define VG_X86_FEAT_PAT		(0*32 + 16)
#define VG_X86_FEAT_PSE36	(0*32 + 17)
#define VG_X86_FEAT_CLFSH	(0*32 + 19)
#define VG_X86_FEAT_DS		(0*32 + 21)
#define VG_X86_FEAT_ACPI	(0*32 + 22)
#define VG_X86_FEAT_MMX		(0*32 + 23)
#define VG_X86_FEAT_FXSR	(0*32 + 24)
#define VG_X86_FEAT_SSE		(0*32 + 25)
#define VG_X86_FEAT_SSE2	(0*32 + 26)
#define VG_X86_FEAT_SS		(0*32 + 27)
#define VG_X86_FEAT_HT		(0*32 + 28)
#define VG_X86_FEAT_TM		(0*32 + 29)
#define VG_X86_FEAT_PBE		(0*32 + 31)

#define VG_X86_FEAT_EST		(1*32 + 7)
#define VG_X86_FEAT_TM2		(1*32 + 8)
#define VG_X86_FEAT_CNXTID	(1*32 + 10)

/* Used internally to mark whether CPUID is even implemented */
#define VG_X86_FEAT_CPUID	(2*32 + 0)

/* The set of features we're willing to support for the client */
#define VG_SUPPORTED_FEATURES			\
	((1 << VG_X86_FEAT_FPU)  |		\
	 (1 << VG_X86_FEAT_TSC)  |		\
	 (1 << VG_X86_FEAT_CMOV) |		\
	 (1 << VG_X86_FEAT_MMX)  |		\
	 (1 << VG_X86_FEAT_FXSR) |		\
	 (1 << VG_X86_FEAT_SSE)  |		\
	 (1 << VG_X86_FEAT_SSE2))
 
/* Various environment variables we pay attention to */

/* The directory we look for all our auxillary files in */
#define VALGRINDLIB	"VALGRINDLIB"

/* Additional command-line arguments; they are overridden by actual
   command-line option.  Each argument is separated by spaces.  There
   is no quoting mechanism.
 */
#define VALGRINDOPTS	"VALGRIND_OPTS"

/* If this variable is present in the environment, then valgrind will
   not parse the command line for options at all; all options come
   from this variable.  Arguments are terminated by ^A (\001).  There
   is no quoting mechanism.

   This variable is not expected to be set by anything other than
   Valgrind itself, as part of its handling of execve with
   --trace-children=yes.  This variable should not be present in the
   client environment.
 */
#define VALGRINDCLO	"_VALGRIND_CLO"

#endif /* ndef __VG_CONSTANTS_H */

/*--------------------------------------------------------------------*/
/*--- end                                           vg_constants.h ---*/
/*--------------------------------------------------------------------*/

/*--------------------------------------------------------------------*/
/*---                                          arm/core_arch_asm.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, an extensible x86 protected-mode
   emulator for monitoring program execution on x86-Unixes.

   Copyright (C) 2000-2004 Nicholas Nethercote
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

#ifndef __ARM_CORE_ARCH_ASM_H
#define __ARM_CORE_ARCH_ASM_H

#if 0
/* size of call instruction put into generated code at jump sites */
#define VG_PATCHME_CALLSZ	5

/* size of jmp instruction which overwrites the call */
#define VG_PATCHME_JMPSZ	5
#endif

// XXX: ???
/* maximum number of normal jumps which can appear in a basic block */
#define VG_MAX_JUMPS		2

/* Offset of code in a TCEntry */
#define VG_CODE_OFFSET		(8 + VG_MAX_JUMPS * 2)

#if 0
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
#endif

#endif   // __ARM_CORE_ARCH_ASM_H

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/

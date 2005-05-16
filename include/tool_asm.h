
/*--------------------------------------------------------------------*/
/*--- Tool-specific, asm-specific includes.             tool_asm.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2000-2005 Julian Seward 
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

#ifndef __TOOL_ASM_H
#define __TOOL_ASM_H


/* All symbols externally visible from valgrind.so are prefixed
   as specified here.  The prefix can be changed, so as to avoid
   namespace conflict problems.
*/
#define VGAPPEND(str1,str2) str1##str2

/* These macros should add different prefixes so the same base
   name can safely be used across different macros. */
#define VG_(str)    VGAPPEND(vgPlain_,str)
#define VGA_(str)   VGAPPEND(vgArch_,str)
#define VGO_(str)   VGAPPEND(vgOS_,str)
#define VGP_(str)   VGAPPEND(vgPlatform_,str)

// Print a constant from asm code.
// Nb: you'll need to define VG_(oynk)(Int) to use this.
#if defined(VGA_x86)
#  define OYNK(nnn) pushal;  pushl $nnn; call VG_(oynk) ; addl $4,%esp; popal
#elif defined(VGA_amd64)
#  define OYNK(nnn) push %r8 ; push %r9 ; push %r10; push %r11; \
                    push %rax; push %rbx; push %rcx; push %rdx; \
                    push %rsi; push %rdi; \
                    movl $nnn, %edi; call VG_(oynk); \
                    pop %rdi; pop %rsi; pop %rdx; pop %rcx; \
                    pop %rbx; pop %rax; pop %r11; pop %r10; \
                    pop %r9 ; pop %r8
#else
#  error Unknown architecture
#endif

#endif /* ndef __TOOL_ASM_H */

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/

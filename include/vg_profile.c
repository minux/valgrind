
/*--------------------------------------------------------------------*/
/*--- Profiling machinery.  #include this file into a skin to      ---*/
/*--- enable --profile=yes, but not for release versions of skins, ---*/
/*--- because it uses glibc code.                                  ---*/
/*---                                                 vg_profile.c ---*/
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

#ifndef __VG_PROFILE_C
#define __VG_PROFILE_C

#include "vg_skin.h"

/* get rid of these, if possible */
#include <signal.h>
#include <sys/time.h>

/* Override the empty definitions from vg_skin.h */
#undef  VGP_PUSHCC
#undef  VGP_POPCC
#define VGP_PUSHCC(x)   if (VG_(clo_profile)) VGP_(pushcc)(x)
#define VGP_POPCC(x)    if (VG_(clo_profile)) VGP_(popcc)(x)

#define VGP_M_STACK     20
#define VGP_MAX_CCS     50


/* All zeroed initially because they're static */
static Int   vgp_nticks;

static Int   vgp_counts [VGP_MAX_CCS];
static Int   vgp_entries[VGP_MAX_CCS];
static Char* vgp_names  [VGP_MAX_CCS];

static Int   vgp_sp;
static UInt  vgp_stack[VGP_M_STACK];

/* These definitions override the panicking ones in vg_profile.c */

void VGP_(register_profile_event) ( Int n, Char* name )
{
   /* Adjust for negative values */
   n += VgpUnc;
   if (n >= VGP_MAX_CCS) {
      VG_(printf)("\nProfile event #%d higher than VGP_MAX_CCS of %d.\n"
                  "If you really need this many profile events, increase\n"
                  "VGP_MAX_CCS and recompile Valgrind.\n",
                  n, VGP_MAX_CCS);
      VG_(skin_panic)("profile event too high");
   }
   if (vgp_names[n] != NULL) {
      VG_(printf)("\nProfile event #%d being registered as `%s'\n"
                  "already registered as `%s'.\n"
                  "Note that skin and core event numbers must not overlap.\n",
                  n, name, vgp_names[n]);
      VG_(skin_panic)("profile event already registered");
   }

   vgp_names[n] = name;
}

void VGP_(tick) ( int sigNo )
{
   Int cc;
   vgp_nticks++;
   cc = vgp_stack[vgp_sp];
   sk_assert(cc >= 0 && cc < VGP_MAX_CCS);
   vgp_counts[ cc ]++;
}

void VGP_(init_profiling) ( void )
{
   struct itimerval value;
   Int ret;

   /* Register core events... tricky macro definition causes
      VGP_(register_profile_event)() to be called once for each core event
      in VGP_CORE_LIST. */
   sk_assert(VgpUnc == 0);
#  define VGP_PAIR(n,name) VGP_(register_profile_event)(n,name)
   VGP_CORE_LIST;
#  undef  VGP_PAIR

   vgp_sp = -1;
   VGP_(pushcc) ( VgpUnc );

   value.it_interval.tv_sec  = 0;
   value.it_interval.tv_usec = 10 * 1000;
   value.it_value = value.it_interval;

   signal(SIGPROF, VGP_(tick) );
   ret = setitimer(ITIMER_PROF, &value, NULL);
   if (ret != 0) VG_(skin_panic)("vgp_init_profiling");
}

void VGP_(done_profiling) ( void )
{
   Int i;
   VG_(printf)("\nProfiling done, %d ticks\n", vgp_nticks);
   for (i = 0; i < VGP_MAX_CCS; i++)
      if (NULL != vgp_names[i])
         VG_(printf)(
            "%2d: %4d (%3d %%%%) ticks,  %10d entries   for  %s\n",
            i, vgp_counts[i], 
            (Int)(1000.0 * (double)vgp_counts[i] / (double)vgp_nticks),
            vgp_entries[i], vgp_names[i] );
}

void VGP_(pushcc) ( UInt cc )
{
   if (vgp_sp >= VGP_M_STACK-1) { 
      VG_(printf)(
         "\nMaximum profile stack depth (%d) reached for event #%d (`%s').\n"
         "This is probably due to a VGP_(pushcc)() without a matching\n"
         "VGP_(popcc)().  Make sure they all match.\n"
         "Or if you are nesting profiling events very deeply, increase\n"
         "VGP_M_STACK and recompile Valgrind.\n",
         VGP_M_STACK, cc, vgp_names[cc]);
      VG_(skin_panic)("Profiling stack overflow");
   }
   vgp_sp++;
   vgp_stack[vgp_sp] = cc;
   vgp_entries[ cc ] ++;
}

void VGP_(popcc) ( UInt cc )
{
   if (vgp_sp <= 0) {
      VG_(printf)(
         "\nProfile stack underflow.  This is due to a VGP_(popcc)() without\n"
         "a matching VGP_(pushcc)().  Make sure they all match.\n");
      VG_(skin_panic)("Profiling stack underflow");
   }
   if (vgp_stack[vgp_sp] != cc) {
      Int i;
      VG_(printf)("popping %s, stack looks like:\n", vgp_names[cc]);
      for (i = vgp_sp; i >= 0; i--)
         VG_(printf)("%2d: %s\n", i, vgp_names[vgp_stack[i]]);
      VG_(exit)(1);
   }
   vgp_sp--;
}

#endif /* __VG_PROFILE_C */

/*--------------------------------------------------------------------*/
/*--- end                                             vg_profile.c ---*/
/*--------------------------------------------------------------------*/

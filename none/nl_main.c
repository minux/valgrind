
/*--------------------------------------------------------------------*/
/*--- Nulgrind: The null skin.                           nl_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Nulgrind, the simplest possible Valgrind skin,
   which does nothing.

   Copyright (C) 2002 Nicholas Nethercote
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

#include "vg_skin.h"

VG_DETERMINE_INTERFACE_VERSION

void SK_(pre_clo_init)(void)
{
   VG_(details_name)            ("Nulgrind");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("a binary JIT-compiler");
   VG_(details_copyright_author)(
      "Copyright (C) 2002, and GNU GPL'd, by Nicholas Nethercote.");
   VG_(details_bug_reports_to)  ("njn25@cam.ac.uk");
   VG_(details_avg_translation_sizeB) ( 76 );

   /* No needs, no core events to track */
}

void SK_(post_clo_init)(void)
{
}

UCodeBlock* SK_(instrument)(UCodeBlock* cb, Addr a)
{
    return cb;
}

void SK_(fini)(void)
{
}

/*--------------------------------------------------------------------*/
/*--- end                                                nl_main.c ---*/
/*--------------------------------------------------------------------*/


/*--------------------------------------------------------------------*/
/*--- For when the client advises Valgrind about memory            ---*/
/*--- permissions.                                                 ---*/
/*---                                              mc_clientreqs.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of MemCheck, a heavyweight Valgrind skin for
   detecting memory errors.

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

#include "mc_include.h"

#include "memcheck.h"  /* for VG_USERREQ__* */


/*------------------------------------------------------------*/
/*--- General client block management.                     ---*/
/*------------------------------------------------------------*/

/* This is managed as an expanding array of client block descriptors.
   Indices of live descriptors are issued to the client, so it can ask
   to free them later.  Therefore we cannot slide live entries down
   over dead ones.  Instead we must use free/inuse flags and scan for
   an empty slot at allocation time.  This in turn means allocation is
   relatively expensive, so we hope this does not happen too often. 
*/

typedef
   enum { CG_NotInUse, CG_NoAccess, CG_Writable, CG_Readable }
   CGenBlockKind;

typedef
   struct {
      Addr          start;
      UInt          size;
      ExeContext*   where;
      CGenBlockKind kind;
   } 
   CGenBlock;

/* This subsystem is self-initialising. */
static UInt       vg_cgb_size = 0;
static UInt       vg_cgb_used = 0;
static CGenBlock* vg_cgbs     = NULL;

/* Stats for this subsystem. */
static UInt vg_cgb_used_MAX = 0;   /* Max in use. */
static UInt vg_cgb_allocs   = 0;   /* Number of allocs. */
static UInt vg_cgb_discards = 0;   /* Number of discards. */
static UInt vg_cgb_search   = 0;   /* Number of searches. */


static
Int vg_alloc_client_block ( void )
{
   Int        i, sz_new;
   CGenBlock* cgbs_new;

   vg_cgb_allocs++;

   for (i = 0; i < vg_cgb_used; i++) {
      vg_cgb_search++;
      if (vg_cgbs[i].kind == CG_NotInUse)
         return i;
   }

   /* Not found.  Try to allocate one at the end. */
   if (vg_cgb_used < vg_cgb_size) {
      vg_cgb_used++;
      return vg_cgb_used-1;
   }

   /* Ok, we have to allocate a new one. */
   sk_assert(vg_cgb_used == vg_cgb_size);
   sz_new = (vg_cgbs == NULL) ? 10 : (2 * vg_cgb_size);

   cgbs_new = VG_(malloc)( sz_new * sizeof(CGenBlock) );
   for (i = 0; i < vg_cgb_used; i++) 
      cgbs_new[i] = vg_cgbs[i];

   if (vg_cgbs != NULL)
      VG_(free)( vg_cgbs );
   vg_cgbs = cgbs_new;

   vg_cgb_size = sz_new;
   vg_cgb_used++;
   if (vg_cgb_used > vg_cgb_used_MAX)
      vg_cgb_used_MAX = vg_cgb_used;
   return vg_cgb_used-1;
}


/*------------------------------------------------------------*/
/*--- Stack block management.                              ---*/
/*------------------------------------------------------------*/

/* This is managed as an expanding array of CStackBlocks.  They are
   packed up against the left-hand end of the array, with no holes.
   They are kept sorted by the start field, with the [0] having the
   highest value.  This means it's pretty cheap to put new blocks at
   the end, corresponding to stack pushes, since the additions put
   blocks on in what is presumably fairly close to strictly descending
   order.  If this assumption doesn't hold the performance
   consequences will be horrible.

   When the client's %ESP jumps back upwards as the result of a RET
   insn, we shrink the array backwards from the end, in a
   guaranteed-cheap linear scan.  
*/

typedef
   struct {
      Addr        start;
      UInt        size;
      ExeContext* where;
   } 
   CStackBlock;

/* This subsystem is self-initialising. */
static UInt         vg_csb_size = 0;
static UInt         vg_csb_used = 0;
static CStackBlock* vg_csbs     = NULL;

/* Stats for this subsystem. */
static UInt vg_csb_used_MAX = 0;   /* Max in use. */
static UInt vg_csb_allocs   = 0;   /* Number of allocs. */
static UInt vg_csb_discards = 0;   /* Number of discards. */
static UInt vg_csb_swaps    = 0;   /* Number of searches. */

static
void vg_add_client_stack_block ( ThreadState* tst, Addr aa, UInt sz )
{
   UInt i, sz_new;
   CStackBlock* csbs_new;
   vg_csb_allocs++;

   /* Ensure there is space for a new block. */

   if (vg_csb_used >= vg_csb_size) {

      /* No; we have to expand the array. */
      sk_assert(vg_csb_used == vg_csb_size);

      sz_new = (vg_csbs == NULL) ? 10 : (2 * vg_csb_size);

      csbs_new = VG_(malloc)( sz_new * sizeof(CStackBlock) );
      for (i = 0; i < vg_csb_used; i++) 
        csbs_new[i] = vg_csbs[i];

      if (vg_csbs != NULL)
         VG_(free)( vg_csbs );
      vg_csbs = csbs_new;

      vg_csb_size = sz_new;
   }

   /* Ok, we can use [vg_csb_used]. */
   vg_csbs[vg_csb_used].start = aa;
   vg_csbs[vg_csb_used].size  = sz;
   /* Actually running a thread at this point. */
   vg_csbs[vg_csb_used].where = VG_(get_ExeContext) ( tst );
   vg_csb_used++;

   if (vg_csb_used > vg_csb_used_MAX)
      vg_csb_used_MAX = vg_csb_used;

   sk_assert(vg_csb_used <= vg_csb_size);

   /* VG_(printf)("acsb  %p %d\n", aa, sz); */
   SK_(make_noaccess) ( aa, sz );

   /* And make sure that they are in descending order of address. */
   i = vg_csb_used;
   while (i > 0 && vg_csbs[i-1].start < vg_csbs[i].start) {
      CStackBlock tmp = vg_csbs[i-1];
      vg_csbs[i-1] = vg_csbs[i];
      vg_csbs[i] = tmp;
      vg_csb_swaps++;
   }

#  if 1
   for (i = 1; i < vg_csb_used; i++)
      sk_assert(vg_csbs[i-1].start >= vg_csbs[i].start);
#  endif
}


/*------------------------------------------------------------*/
/*--- Externally visible functions.                        ---*/
/*------------------------------------------------------------*/

void SK_(show_client_block_stats) ( void )
{
   VG_(message)(Vg_DebugMsg, 
      "general CBs: %d allocs, %d discards, %d maxinuse, %d search",
      vg_cgb_allocs, vg_cgb_discards, vg_cgb_used_MAX, vg_cgb_search 
   );
   VG_(message)(Vg_DebugMsg, 
      "  stack CBs: %d allocs, %d discards, %d maxinuse, %d swap",
      vg_csb_allocs, vg_csb_discards, vg_csb_used_MAX, vg_csb_swaps
   );
}

Bool SK_(client_perm_maybe_describe)( Addr a, AddrInfo* ai )
{
   Int i;
   /* VG_(printf)("try to identify %d\n", a); */

   /* First see if it's a stack block.  We do two passes, one exact
      and one with a bit of slop, so as to try and get the most
      accurate fix. */
   for (i = 0; i < vg_csb_used; i++) {
      if (vg_csbs[i].start <= a
          && a < vg_csbs[i].start + vg_csbs[i].size) {
         ai->akind = UserS;
         ai->blksize = vg_csbs[i].size;
         ai->rwoffset  = (Int)(a) - (Int)(vg_csbs[i].start);
         ai->lastchange = vg_csbs[i].where;
         return True;
      }
   }

   /* No exact match on the stack.  Re-do the stack scan with a bit of
      slop. */
   for (i = 0; i < vg_csb_used; i++) {
      if (vg_csbs[i].start - 8 <= a
          && a < vg_csbs[i].start + vg_csbs[i].size + 8) {
         ai->akind = UserS;
         ai->blksize = vg_csbs[i].size;
         ai->rwoffset  = (Int)(a) - (Int)(vg_csbs[i].start);
         ai->lastchange = vg_csbs[i].where;
         return True;
      }
   }

   /* No match on the stack.  Perhaps it's a general block ? */
   for (i = 0; i < vg_cgb_used; i++) {
      if (vg_cgbs[i].kind == CG_NotInUse) 
         continue;
      if (VG_(addr_is_in_block)(a, vg_cgbs[i].start, vg_cgbs[i].size)) {
         ai->akind = UserG;
         ai->blksize = vg_cgbs[i].size;
         ai->rwoffset  = (Int)(a) - (Int)(vg_cgbs[i].start);
         ai->lastchange = vg_cgbs[i].where;
         return True;
      }
   }
   return False;
}


void SK_(delete_client_stack_blocks_following_ESP_change) ( void )
{
   Addr newESP = VG_(get_stack_pointer)();

   while (vg_csb_used > 0 
          && vg_csbs[vg_csb_used-1].start + vg_csbs[vg_csb_used-1].size 
             <= newESP) {
      vg_csb_used--;
      vg_csb_discards++;
      if (VG_(clo_verbosity) > 2)
         VG_(printf)("discarding stack block %p for %d\n", 
            (void*)vg_csbs[vg_csb_used].start, 
            vg_csbs[vg_csb_used].size);
   }
}


Bool SK_(handle_client_request) ( ThreadState* tst, UInt* arg_block, UInt *ret )
{
   Int   i;
   Bool  ok;
   Addr  bad_addr;
   UInt* arg = arg_block;

   if (!VG_IS_SKIN_USERREQ('M','C',arg[0]))
      return False;

   switch (arg[0]) {
      case VG_USERREQ__CHECK_WRITABLE: /* check writable */
         ok = SK_(check_writable) ( arg[1], arg[2], &bad_addr );
         if (!ok)
            SK_(record_user_error) ( tst, bad_addr, True );
         *ret = ok ? (UInt)NULL : bad_addr;
	 break;

      case VG_USERREQ__CHECK_READABLE: /* check readable */
         ok = SK_(check_readable) ( arg[1], arg[2], &bad_addr );
         if (!ok)
            SK_(record_user_error) ( tst, bad_addr, False );
         *ret = ok ? (UInt)NULL : bad_addr;
	 break;

      case VG_USERREQ__DO_LEAK_CHECK:
         SK_(detect_memory_leaks)();
	 *ret = 0; /* return value is meaningless */
	 break;

      case VG_USERREQ__MAKE_NOACCESS: /* make no access */
         i = vg_alloc_client_block();
         /* VG_(printf)("allocated %d %p\n", i, vg_cgbs); */
         vg_cgbs[i].kind  = CG_NoAccess;
         vg_cgbs[i].start = arg[1];
         vg_cgbs[i].size  = arg[2];
         vg_cgbs[i].where = VG_(get_ExeContext) ( tst );
         SK_(make_noaccess) ( arg[1], arg[2] );
	 *ret = i;
	 break;

      case VG_USERREQ__MAKE_WRITABLE: /* make writable */
         i = vg_alloc_client_block();
         vg_cgbs[i].kind  = CG_Writable;
         vg_cgbs[i].start = arg[1];
         vg_cgbs[i].size  = arg[2];
         vg_cgbs[i].where = VG_(get_ExeContext) ( tst );
         SK_(make_writable) ( arg[1], arg[2] );
         *ret = i;
	 break;

      case VG_USERREQ__MAKE_READABLE: /* make readable */
         i = vg_alloc_client_block();
         vg_cgbs[i].kind  = CG_Readable;
         vg_cgbs[i].start = arg[1];
         vg_cgbs[i].size  = arg[2];
         vg_cgbs[i].where = VG_(get_ExeContext) ( tst );
         SK_(make_readable) ( arg[1], arg[2] );
	 *ret = i;
         break;

      case VG_USERREQ__DISCARD: /* discard */
         if (vg_cgbs == NULL 
             || arg[2] >= vg_cgb_used || vg_cgbs[arg[2]].kind == CG_NotInUse)
            return 1;
         sk_assert(arg[2] >= 0 && arg[2] < vg_cgb_used);
         vg_cgbs[arg[2]].kind = CG_NotInUse;
         vg_cgb_discards++;
	 *ret = 0;
	 break;

      case VG_USERREQ__MAKE_NOACCESS_STACK: /* make noaccess stack block */
         vg_add_client_stack_block ( tst, arg[1], arg[2] );
	 *ret = 0;
	 break;

      default:
         VG_(message)(Vg_UserMsg, 
                      "Warning: unknown memcheck client request code %d",
                      arg[0]);
         return False;
   }
   return True;
}


/*--------------------------------------------------------------------*/
/*--- end                                          mc_clientreqs.c ---*/
/*--------------------------------------------------------------------*/

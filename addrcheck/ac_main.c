
/*--------------------------------------------------------------------*/
/*--- The AddrCheck tool: like MemCheck, but only does address     ---*/
/*--- checking.  No definedness checking.                          ---*/
/*---                                                    ac_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of AddrCheck, a lightweight Valgrind tool for
   detecting memory errors.

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

#include "mac_shared.h"
#include "memcheck.h"
//#include "vg_profile.c"


/*------------------------------------------------------------*/
/*--- Comparing and printing errors                        ---*/
/*------------------------------------------------------------*/

void SK_(pp_SkinError) ( Error* err )
{
   MAC_Error* err_extra = VG_(get_error_extra)(err);

   switch (VG_(get_error_kind)(err)) {
      case CoreMemErr:
         VG_(message)(Vg_UserMsg, "%s contains unaddressable byte(s)", 
         VG_(get_error_string)(err));
         VG_(pp_ExeContext)( VG_(get_error_where)(err) );
         break;
      
      case ParamErr:
         VG_(message)(Vg_UserMsg, 
                      "Syscall param %s contains unaddressable byte(s)",
                      VG_(get_error_string)(err) );
         VG_(pp_ExeContext)( VG_(get_error_where)(err) );
         MAC_(pp_AddrInfo)(VG_(get_error_address)(err), &err_extra->addrinfo);
         break;

      case UserErr:
         VG_(message)(Vg_UserMsg, 
            "Unaddressable byte(s) found during client check request");
         VG_(pp_ExeContext)( VG_(get_error_where)(err) );
         MAC_(pp_AddrInfo)(VG_(get_error_address)(err), &err_extra->addrinfo);
         break;

      default: 
         MAC_(pp_shared_SkinError)(err);
         break;
   }
}

/*------------------------------------------------------------*/
/*--- Suppressions                                         ---*/
/*------------------------------------------------------------*/

Bool SK_(recognised_suppression) ( Char* name, Supp* su )
{
   return MAC_(shared_recognised_suppression)(name, su);
}

#define DEBUG(fmt, args...) //VG_(printf)(fmt, ## args)

/*------------------------------------------------------------*/
/*--- Low-level support for memory checking.               ---*/
/*------------------------------------------------------------*/

/* All reads and writes are checked against a memory map, which
   records the state of all memory in the process.  The memory map is
   organised like this:

   The top 16 bits of an address are used to index into a top-level
   map table, containing 65536 entries.  Each entry is a pointer to a
   second-level map, which records the accesibililty and validity
   permissions for the 65536 bytes indexed by the lower 16 bits of the
   address.  Each byte is represented by one bit, indicating
   accessibility.  So each second-level map contains 8192 bytes.  This
   two-level arrangement conveniently divides the 4G address space
   into 64k lumps, each size 64k bytes.

   All entries in the primary (top-level) map must point to a valid
   secondary (second-level) map.  Since most of the 4G of address
   space will not be in use -- ie, not mapped at all -- there is a
   distinguished secondary map, which indicates `not addressible and
   not valid' writeable for all bytes.  Entries in the primary map for
   which the entire 64k is not in use at all point at this
   distinguished map.

   [...] lots of stuff deleted due to out of date-ness

   As a final optimisation, the alignment and address checks for
   4-byte loads and stores are combined in a neat way.  The primary
   map is extended to have 262144 entries (2^18), rather than 2^16.
   The top 3/4 of these entries are permanently set to the
   distinguished secondary map.  For a 4-byte load/store, the
   top-level map is indexed not with (addr >> 16) but instead f(addr),
   where

    f( XXXX XXXX XXXX XXXX ____ ____ ____ __YZ )
        = ____ ____ ____ __YZ XXXX XXXX XXXX XXXX  or 
        = ____ ____ ____ __ZY XXXX XXXX XXXX XXXX

   ie the lowest two bits are placed above the 16 high address bits.
   If either of these two bits are nonzero, the address is misaligned;
   this will select a secondary map from the upper 3/4 of the primary
   map.  Because this is always the distinguished secondary map, a
   (bogus) address check failure will result.  The failure handling
   code can then figure out whether this is a genuine addr check
   failure or whether it is a possibly-legitimate access at a
   misaligned address.  */


/*------------------------------------------------------------*/
/*--- Function declarations.                               ---*/
/*------------------------------------------------------------*/

static void ac_ACCESS4_SLOWLY ( Addr a, Bool isWrite );
static void ac_ACCESS2_SLOWLY ( Addr a, Bool isWrite );
static void ac_ACCESS1_SLOWLY ( Addr a, Bool isWrite );
static void ac_fpu_ACCESS_check_SLOWLY ( Addr addr, Int size, Bool isWrite );

/*------------------------------------------------------------*/
/*--- Data defns.                                          ---*/
/*------------------------------------------------------------*/

typedef 
   struct {
      UChar abits[8192];
   }
   AcSecMap;

static AcSecMap* primary_map[ /*65536*/ 262144 ];
static AcSecMap  distinguished_secondary_map;

static void init_shadow_memory ( void )
{
   Int i;

   for (i = 0; i < 8192; i++)             /* Invalid address */
      distinguished_secondary_map.abits[i] = VGM_BYTE_INVALID; 

   /* These entries gradually get overwritten as the used address
      space expands. */
   for (i = 0; i < 65536; i++)
      primary_map[i] = &distinguished_secondary_map;

   /* These ones should never change; it's a bug in Valgrind if they do. */
   for (i = 65536; i < 262144; i++)
      primary_map[i] = &distinguished_secondary_map;
}

/*------------------------------------------------------------*/
/*--- Basic bitmap management, reading and writing.        ---*/
/*------------------------------------------------------------*/

/* Allocate and initialise a secondary map. */

static AcSecMap* alloc_secondary_map ( __attribute__ ((unused)) 
                                       Char* caller )
{
   AcSecMap* map;
   UInt  i;
   PROF_EVENT(10);

   /* Mark all bytes as invalid access and invalid value. */
   map = (AcSecMap *)VG_(shadow_alloc)(sizeof(AcSecMap));
   for (i = 0; i < 8192; i++)
      map->abits[i] = VGM_BYTE_INVALID; /* Invalid address */

   /* VG_(printf)("ALLOC_2MAP(%s)\n", caller ); */
   return map;
}


/* Basic reading/writing of the bitmaps, for byte-sized accesses. */

static __inline__ UChar get_abit ( Addr a )
{
   AcSecMap* sm     = primary_map[a >> 16];
   UInt    sm_off = a & 0xFFFF;
   PROF_EVENT(20);
#  if 0
      if (IS_DISTINGUISHED_SM(sm))
         VG_(message)(Vg_DebugMsg, 
                      "accessed distinguished 2ndary (A)map! 0x%x\n", a);
#  endif
   return BITARR_TEST(sm->abits, sm_off) 
             ? VGM_BIT_INVALID : VGM_BIT_VALID;
}

static /* __inline__ */ void set_abit ( Addr a, UChar abit )
{
   AcSecMap* sm;
   UInt    sm_off;
   PROF_EVENT(22);
   ENSURE_MAPPABLE(a, "set_abit");
   sm     = primary_map[a >> 16];
   sm_off = a & 0xFFFF;
   if (abit) 
      BITARR_SET(sm->abits, sm_off);
   else
      BITARR_CLEAR(sm->abits, sm_off);
}


/* Reading/writing of the bitmaps, for aligned word-sized accesses. */

static __inline__ UChar get_abits4_ALIGNED ( Addr a )
{
   AcSecMap* sm;
   UInt    sm_off;
   UChar   abits8;
   PROF_EVENT(24);
#  ifdef VG_DEBUG_MEMORY
   sk_assert(IS_ALIGNED4_ADDR(a));
#  endif
   sm     = primary_map[a >> 16];
   sm_off = a & 0xFFFF;
   abits8 = sm->abits[sm_off >> 3];
   abits8 >>= (a & 4 /* 100b */);   /* a & 4 is either 0 or 4 */
   abits8 &= 0x0F;
   return abits8;
}



/*------------------------------------------------------------*/
/*--- Setting permissions over address ranges.             ---*/
/*------------------------------------------------------------*/

static /* __inline__ */
void set_address_range_perms ( Addr a, SizeT len, UInt example_a_bit )
{
   UChar     abyte8;
   UInt      sm_off;
   AcSecMap* sm;

   PROF_EVENT(30);

   if (len == 0)
      return;

   if (len > 100 * 1000 * 1000) {
      VG_(message)(Vg_UserMsg, 
                   "Warning: set address range perms: "
                   "large range %u, a %d",
                   len, example_a_bit );
   }

   VGP_PUSHCC(VgpSetMem);

   /* Requests to change permissions of huge address ranges may
      indicate bugs in our machinery.  30,000,000 is arbitrary, but so
      far all legitimate requests have fallen beneath that size. */
   /* 4 Mar 02: this is just stupid; get rid of it. */
   /* sk_assert(len < 30000000); */

   /* Check the permissions make sense. */
   sk_assert(example_a_bit == VGM_BIT_VALID 
             || example_a_bit == VGM_BIT_INVALID);

   /* In order that we can charge through the address space at 8
      bytes/main-loop iteration, make up some perms. */
   abyte8 = (example_a_bit << 7)
            | (example_a_bit << 6)
            | (example_a_bit << 5)
            | (example_a_bit << 4)
            | (example_a_bit << 3)
            | (example_a_bit << 2)
            | (example_a_bit << 1)
            | (example_a_bit << 0);

#  ifdef VG_DEBUG_MEMORY
   /* Do it ... */
   while (True) {
      PROF_EVENT(31);
      if (len == 0) break;
      set_abit ( a, example_a_bit );
      set_vbyte ( a, vbyte );
      a++;
      len--;
   }

#  else
   /* Slowly do parts preceding 8-byte alignment. */
   while (True) {
      PROF_EVENT(31);
      if (len == 0) break;
      if ((a % 8) == 0) break;
      set_abit ( a, example_a_bit );
      a++;
      len--;
   }   

   if (len == 0) {
      VGP_POPCC(VgpSetMem);
      return;
   }
   sk_assert((a % 8) == 0 && len > 0);

   /* Once aligned, go fast. */
   while (True) {
      PROF_EVENT(32);
      if (len < 8) break;
      ENSURE_MAPPABLE(a, "set_address_range_perms(fast)");
      sm = primary_map[a >> 16];
      sm_off = a & 0xFFFF;
      sm->abits[sm_off >> 3] = abyte8;
      a += 8;
      len -= 8;
   }

   if (len == 0) {
      VGP_POPCC(VgpSetMem);
      return;
   }
   sk_assert((a % 8) == 0 && len > 0 && len < 8);

   /* Finish the upper fragment. */
   while (True) {
      PROF_EVENT(33);
      if (len == 0) break;
      set_abit ( a, example_a_bit );
      a++;
      len--;
   }   
#  endif

   /* Check that zero page and highest page have not been written to
      -- this could happen with buggy syscall wrappers.  Today
      (2001-04-26) had precisely such a problem with __NR_setitimer. */
   sk_assert(SK_(cheap_sanity_check)());
   VGP_POPCC(VgpSetMem);
}

/* Set permissions for address ranges ... */

static void ac_make_noaccess ( Addr a, SizeT len )
{
   PROF_EVENT(35);
   DEBUG("ac_make_noaccess(%p, %x)\n", a, len);
   set_address_range_perms ( a, len, VGM_BIT_INVALID );
}

static void ac_make_accessible ( Addr a, SizeT len )
{
   PROF_EVENT(38);
   DEBUG("ac_make_accessible(%p, %x)\n", a, len);
   set_address_range_perms ( a, len, VGM_BIT_VALID );
}

static __inline__
void make_aligned_word_noaccess(Addr a)
{
   AcSecMap* sm;
   UInt      sm_off;
   UChar     mask;

   VGP_PUSHCC(VgpESPAdj);
   ENSURE_MAPPABLE(a, "make_aligned_word_noaccess");
   sm     = primary_map[a >> 16];
   sm_off = a & 0xFFFF;
   mask = 0x0F;
   mask <<= (a & 4 /* 100b */);   /* a & 4 is either 0 or 4 */
   /* mask now contains 1s where we wish to make address bits invalid (1s). */
   sm->abits[sm_off >> 3] |= mask;
   VGP_POPCC(VgpESPAdj);
}

static __inline__
void make_aligned_word_accessible(Addr a)
{
   AcSecMap* sm;
   UInt      sm_off;
   UChar     mask;

   VGP_PUSHCC(VgpESPAdj);
   ENSURE_MAPPABLE(a, "make_aligned_word_accessible");
   sm     = primary_map[a >> 16];
   sm_off = a & 0xFFFF;
   mask = 0x0F;
   mask <<= (a & 4 /* 100b */);   /* a & 4 is either 0 or 4 */
   /* mask now contains 1s where we wish to make address bits
      invalid (0s). */
   sm->abits[sm_off >> 3] &= ~mask;
   VGP_POPCC(VgpESPAdj);
}

/* Nb: by "aligned" here we mean 8-byte aligned */
static __inline__
void make_aligned_doubleword_accessible(Addr a)
{  
   AcSecMap* sm;
   UInt      sm_off;
   
   VGP_PUSHCC(VgpESPAdj);
   ENSURE_MAPPABLE(a, "make_aligned_doubleword_accessible");
   sm = primary_map[a >> 16];
   sm_off = a & 0xFFFF;
   sm->abits[sm_off >> 3] = VGM_BYTE_VALID;
   VGP_POPCC(VgpESPAdj);
}  
   
static __inline__
void make_aligned_doubleword_noaccess(Addr a)
{  
   AcSecMap* sm;
   UInt      sm_off;
   
   VGP_PUSHCC(VgpESPAdj);
   ENSURE_MAPPABLE(a, "make_aligned_doubleword_noaccess");
   sm = primary_map[a >> 16];
   sm_off = a & 0xFFFF;
   sm->abits[sm_off >> 3] = VGM_BYTE_INVALID;
   VGP_POPCC(VgpESPAdj);
}  
   
/* The %esp update handling functions */
ESP_UPDATE_HANDLERS ( make_aligned_word_accessible,  
                      make_aligned_word_noaccess,
                      make_aligned_doubleword_accessible,
                      make_aligned_doubleword_noaccess,
                      ac_make_accessible,
                      ac_make_noaccess 
                    );


/* Block-copy permissions (needed for implementing realloc()). */

static void ac_copy_address_range_state ( Addr src, Addr dst, SizeT len )
{
   UInt i;

   DEBUG("ac_copy_address_range_state\n");

   PROF_EVENT(40);
   for (i = 0; i < len; i++) {
      UChar abit  = get_abit ( src+i );
      PROF_EVENT(41);
      set_abit ( dst+i, abit );
   }
}


/* Check permissions for address range.  If inadequate permissions
   exist, *bad_addr is set to the offending address, so the caller can
   know what it is. */

static __inline__
Bool ac_check_accessible ( Addr a, SizeT len, Addr* bad_addr )
{
   UInt  i;
   UChar abit;
   PROF_EVENT(48);
   for (i = 0; i < len; i++) {
      PROF_EVENT(49);
      abit = get_abit(a);
      if (abit == VGM_BIT_INVALID) {
         if (bad_addr != NULL) *bad_addr = a;
         return False;
      }
      a++;
   }
   return True;
}

/* The opposite; check that an address range is inaccessible. */
static
Bool ac_check_noaccess ( Addr a, SizeT len, Addr* bad_addr )
{
   UInt  i;
   UChar abit;
   PROF_EVENT(48);
   for (i = 0; i < len; i++) {
      PROF_EVENT(49);
      abit = get_abit(a);
      if (abit == VGM_BIT_VALID) {
         if (bad_addr != NULL) *bad_addr = a;
         return False;
      }
      a++;
   }
   return True;
}

/* Check a zero-terminated ascii string.  Tricky -- don't want to
   examine the actual bytes, to find the end, until we're sure it is
   safe to do so. */

static __inline__
Bool ac_check_readable_asciiz ( Addr a, Addr* bad_addr )
{
   UChar abit;
   PROF_EVENT(46);
   DEBUG("ac_check_readable_asciiz\n");
   while (True) {
      PROF_EVENT(47);
      abit  = get_abit(a);
      if (abit != VGM_BIT_VALID) {
         if (bad_addr != NULL) *bad_addr = a;
         return False;
      }
      /* Ok, a is safe to read. */
      if (* ((UChar*)a) == 0) return True;
      a++;
   }
}


/*------------------------------------------------------------*/
/*--- Memory event handlers                                ---*/
/*------------------------------------------------------------*/

static __inline__
void ac_check_is_accessible ( CorePart part, ThreadId tid,
                              Char* s, Addr base, SizeT size, Bool isWrite )
{
   Bool ok;
   Addr bad_addr;

   VGP_PUSHCC(VgpCheckMem);

   ok = ac_check_accessible ( base, size, &bad_addr );
   if (!ok) {
      switch (part) {
      case Vg_CoreSysCall:
         MAC_(record_param_error) ( tid, bad_addr, isWrite, s );
         break;

      case Vg_CoreSignal:
         sk_assert(isWrite);     /* Should only happen with isWrite case */
         /* fall through */
      case Vg_CorePThread:
         MAC_(record_core_mem_error)( tid, isWrite, s );
         break;

      /* If we're being asked to jump to a silly address, record an error 
         message before potentially crashing the entire system. */
      case Vg_CoreTranslate:
         sk_assert(!isWrite);    /* Should only happen with !isWrite case */
         MAC_(record_jump_error)( tid, bad_addr );
         break;

      default:
         VG_(skin_panic)("ac_check_is_accessible: unexpected CorePart");
      }
   }

   VGP_POPCC(VgpCheckMem);
}

static
void ac_check_is_writable ( CorePart part, ThreadId tid,
                            Char* s, Addr base, SizeT size )
{
   ac_check_is_accessible ( part, tid, s, base, size, /*isWrite*/True );
}

static
void ac_check_is_readable ( CorePart part, ThreadId tid,
                            Char* s, Addr base, SizeT size )
{     
   ac_check_is_accessible ( part, tid, s, base, size, /*isWrite*/False );
}

static
void ac_check_is_readable_asciiz ( CorePart part, ThreadId tid,
                                   Char* s, Addr str )
{
   Bool ok = True;
   Addr bad_addr;
   /* VG_(message)(Vg_DebugMsg,"check is readable asciiz: 0x%x",str); */

   VGP_PUSHCC(VgpCheckMem);

   sk_assert(part == Vg_CoreSysCall);
   ok = ac_check_readable_asciiz ( (Addr)str, &bad_addr );
   if (!ok) {
      MAC_(record_param_error) ( tid, bad_addr, /*is_writable =*/False, s );
   }

   VGP_POPCC(VgpCheckMem);
}

static
void ac_new_mem_startup( Addr a, SizeT len, Bool rr, Bool ww, Bool xx )
{
   /* Ignore the permissions, just make it readable.  Seems to work... */
   DEBUG("new_mem_startup(%p, %u, rr=%u, ww=%u, xx=%u)\n", a,len,rr,ww,xx);
   ac_make_accessible(a, len);
}

static
void ac_new_mem_heap ( Addr a, SizeT len, Bool is_inited )
{
   ac_make_accessible(a, len);
}

static
void ac_set_perms (Addr a, SizeT len, Bool rr, Bool ww, Bool xx)
{
   DEBUG("ac_set_perms(%p, %u, rr=%u ww=%u, xx=%u)\n",
                              a, len, rr, ww, xx);
   if (rr || ww || xx) {
      ac_make_accessible(a, len);
   } else {
      ac_make_noaccess(a, len);
   }
}


/*------------------------------------------------------------*/
/*--- Functions called directly from generated code.       ---*/
/*------------------------------------------------------------*/

static __inline__ UInt rotateRight16 ( UInt x )
{
   /* Amazingly, gcc turns this into a single rotate insn. */
   return (x >> 16) | (x << 16);
}

static __inline__ UInt shiftRight16 ( UInt x )
{
   return x >> 16;
}


/* Read/write 1/2/4 sized V bytes, and emit an address error if
   needed. */

/* ac_helperc_ACCESS{1,2,4} handle the common case fast.
   Under all other circumstances, it defers to the relevant _SLOWLY
   function, which can handle all situations.
*/
static __inline__ void ac_helperc_ACCESS4 ( Addr a, Bool isWrite )
{
#  ifdef VG_DEBUG_MEMORY
   return ac_ACCESS4_SLOWLY(a, isWrite);
#  else
   UInt    sec_no = rotateRight16(a) & 0x3FFFF;
   AcSecMap* sm   = primary_map[sec_no];
   UInt    a_off  = (a & 0xFFFF) >> 3;
   UChar   abits  = sm->abits[a_off];
   abits >>= (a & 4);
   abits &= 15;
   PROF_EVENT(66);
   if (abits == VGM_NIBBLE_VALID) {
      /* Handle common case quickly: a is suitably aligned, is mapped,
         and is addressible.  So just return. */
      return;
   } else {
      /* Slow but general case. */
      ac_ACCESS4_SLOWLY(a, isWrite);
   }
#  endif
}

static __inline__ void ac_helperc_ACCESS2 ( Addr a, Bool isWrite )
{
#  ifdef VG_DEBUG_MEMORY
   return ac_ACCESS2_SLOWLY(a, isWrite);
#  else
   UInt    sec_no = rotateRight16(a) & 0x1FFFF;
   AcSecMap* sm     = primary_map[sec_no];
   UInt    a_off  = (a & 0xFFFF) >> 3;
   PROF_EVENT(67);
   if (sm->abits[a_off] == VGM_BYTE_VALID) {
      /* Handle common case quickly. */
      return;
   } else {
      /* Slow but general case. */
      ac_ACCESS2_SLOWLY(a, isWrite);
   }
#  endif
}

static __inline__ void ac_helperc_ACCESS1 ( Addr a, Bool isWrite )
{
#  ifdef VG_DEBUG_MEMORY
   return ac_ACCESS1_SLOWLY(a, isWrite);
#  else
   UInt    sec_no = shiftRight16(a);
   AcSecMap* sm   = primary_map[sec_no];
   UInt    a_off  = (a & 0xFFFF) >> 3;
   PROF_EVENT(68);
   if (sm->abits[a_off] == VGM_BYTE_VALID) {
      /* Handle common case quickly. */
      return;
   } else {
      /* Slow but general case. */
      ac_ACCESS1_SLOWLY(a, isWrite);
   }
#  endif
}

REGPARM(1)
static void ac_helperc_LOAD4 ( Addr a )
{
   ac_helperc_ACCESS4 ( a, /*isWrite*/False );
}
REGPARM(1)
static void ac_helperc_STORE4 ( Addr a )
{
   ac_helperc_ACCESS4 ( a, /*isWrite*/True );
}

REGPARM(1)
static void ac_helperc_LOAD2 ( Addr a )
{
   ac_helperc_ACCESS2 ( a, /*isWrite*/False );
}
REGPARM(1)
static void ac_helperc_STORE2 ( Addr a )
{
   ac_helperc_ACCESS2 ( a, /*isWrite*/True );
}

REGPARM(1)
static void ac_helperc_LOAD1 ( Addr a )
{
   ac_helperc_ACCESS1 ( a, /*isWrite*/False );
}
REGPARM(1)
static void ac_helperc_STORE1 ( Addr a )
{
   ac_helperc_ACCESS1 ( a, /*isWrite*/True );
}


/*------------------------------------------------------------*/
/*--- Fallback functions to handle cases that the above    ---*/
/*--- ac_helperc_ACCESS{1,2,4} can't manage.               ---*/
/*------------------------------------------------------------*/

static void ac_ACCESS4_SLOWLY ( Addr a, Bool isWrite )
{
   Bool a0ok, a1ok, a2ok, a3ok;

   PROF_EVENT(76);

   /* First establish independently the addressibility of the 4 bytes
      involved. */
   a0ok = get_abit(a+0) == VGM_BIT_VALID;
   a1ok = get_abit(a+1) == VGM_BIT_VALID;
   a2ok = get_abit(a+2) == VGM_BIT_VALID;
   a3ok = get_abit(a+3) == VGM_BIT_VALID;

   /* Now distinguish 3 cases */

   /* Case 1: the address is completely valid, so:
      - no addressing error
   */
   if (a0ok && a1ok && a2ok && a3ok) {
      return;
   }

   /* Case 2: the address is completely invalid.  
      - emit addressing error
   */
   /* VG_(printf)("%p (%d %d %d %d)\n", a, a0ok, a1ok, a2ok, a3ok); */
   if (!MAC_(clo_partial_loads_ok) 
       || ((a & 3) != 0)
       || (!a0ok && !a1ok && !a2ok && !a3ok)) {
      MAC_(record_address_error)( VG_(get_current_tid)(), a, 4, isWrite );
      return;
   }

   /* Case 3: the address is partially valid.  
      - no addressing error
      Case 3 is only allowed if MAC_(clo_partial_loads_ok) is True
      (which is the default), and the address is 4-aligned.  
      If not, Case 2 will have applied.
   */
   sk_assert(MAC_(clo_partial_loads_ok));
   {
      return;
   }
}

static void ac_ACCESS2_SLOWLY ( Addr a, Bool isWrite )
{
   /* Check the address for validity. */
   Bool aerr = False;
   PROF_EVENT(77);

   if (get_abit(a+0) != VGM_BIT_VALID) aerr = True;
   if (get_abit(a+1) != VGM_BIT_VALID) aerr = True;

   /* If an address error has happened, report it. */
   if (aerr) {
      MAC_(record_address_error)( VG_(get_current_tid)(), a, 2, isWrite );
   }
}

static void ac_ACCESS1_SLOWLY ( Addr a, Bool isWrite)
{
   /* Check the address for validity. */
   Bool aerr = False;
   PROF_EVENT(78);

   if (get_abit(a+0) != VGM_BIT_VALID) aerr = True;

   /* If an address error has happened, report it. */
   if (aerr) {
      MAC_(record_address_error)( VG_(get_current_tid)(), a, 1, isWrite );
   }
}


/* ---------------------------------------------------------------------
   FPU load and store checks, called from generated code.
   ------------------------------------------------------------------ */

static 
void ac_fpu_ACCESS_check ( Addr addr, Int size, Bool isWrite )
{
   /* Ensure the read area is both addressible and valid (ie,
      readable).  If there's an address error, don't report a value
      error too; but if there isn't an address error, check for a
      value error. 

      Try to be reasonably fast on the common case; wimp out and defer
      to ac_fpu_ACCESS_check_SLOWLY for everything else.  */

   AcSecMap* sm;
   UInt    sm_off, a_off;
   Addr    addr4;

   PROF_EVENT(90);

#  ifdef VG_DEBUG_MEMORY
   ac_fpu_ACCESS_check_SLOWLY ( addr, size, isWrite );
#  else

   if (size == 4) {
      if (!IS_ALIGNED4_ADDR(addr)) goto slow4;
      PROF_EVENT(91);
      /* Properly aligned. */
      sm     = primary_map[addr >> 16];
      sm_off = addr & 0xFFFF;
      a_off  = sm_off >> 3;
      if (sm->abits[a_off] != VGM_BYTE_VALID) goto slow4;
      /* Properly aligned and addressible. */
      return;
     slow4:
      ac_fpu_ACCESS_check_SLOWLY ( addr, 4, isWrite );
      return;
   }

   if (size == 8) {
      if (!IS_ALIGNED4_ADDR(addr)) goto slow8;
      PROF_EVENT(92);
      /* Properly aligned.  Do it in two halves. */
      addr4 = addr + 4;
      /* First half. */
      sm     = primary_map[addr >> 16];
      sm_off = addr & 0xFFFF;
      a_off  = sm_off >> 3;
      if (sm->abits[a_off] != VGM_BYTE_VALID) goto slow8;
      /* First half properly aligned and addressible. */
      /* Second half. */
      sm     = primary_map[addr4 >> 16];
      sm_off = addr4 & 0xFFFF;
      a_off  = sm_off >> 3;
      if (sm->abits[a_off] != VGM_BYTE_VALID) goto slow8;
      /* Second half properly aligned and addressible. */
      /* Both halves properly aligned and addressible. */
      return;
     slow8:
      ac_fpu_ACCESS_check_SLOWLY ( addr, 8, isWrite );
      return;
   }

   /* Can't be bothered to huff'n'puff to make these (allegedly) rare
      cases go quickly.  */
   if (size == 2) {
      PROF_EVENT(93);
      ac_fpu_ACCESS_check_SLOWLY ( addr, 2, isWrite );
      return;
   }

   if (size == 16 || size == 10 || size == 28 || size == 108 || size == 512) {
      PROF_EVENT(94);
      ac_fpu_ACCESS_check_SLOWLY ( addr, size, isWrite );
      return;
   }

   VG_(printf)("size is %d\n", size);
   VG_(skin_panic)("fpu_ACCESS_check: unhandled size");
#  endif
}

REGPARM(2)
static void ac_fpu_READ_check ( Addr addr, Int size )
{
   ac_fpu_ACCESS_check ( addr, size, /*isWrite*/False );
}

REGPARM(2)
static void ac_fpu_WRITE_check ( Addr addr, Int size )
{
   ac_fpu_ACCESS_check ( addr, size, /*isWrite*/True );
}

/* ---------------------------------------------------------------------
   Slow, general cases for FPU access checks.
   ------------------------------------------------------------------ */

void ac_fpu_ACCESS_check_SLOWLY ( Addr addr, Int size, Bool isWrite )
{
   Int  i;
   Bool aerr = False;
   PROF_EVENT(100);
   for (i = 0; i < size; i++) {
      PROF_EVENT(101);
      if (get_abit(addr+i) != VGM_BIT_VALID)
         aerr = True;
   }

   if (aerr) {
      MAC_(record_address_error)( VG_(get_current_tid)(), addr, size, isWrite );
   }
}


/*------------------------------------------------------------*/
/*--- Our instrumenter                                     ---*/
/*------------------------------------------------------------*/

UCodeBlock* SK_(instrument)(UCodeBlock* cb_in, Addr orig_addr)
{
/* Use this rather than eg. -1 because it's a UInt. */
#define INVALID_DATA_SIZE   999999

   UCodeBlock* cb;
   Int         i;
   UInstr*     u_in;
   Int         t_addr, t_size;
   Addr        helper;

   cb = VG_(setup_UCodeBlock)(cb_in);

   for (i = 0; i < VG_(get_num_instrs)(cb_in); i++) {

      t_addr = t_size = INVALID_TEMPREG;
      u_in = VG_(get_instr)(cb_in, i);

      switch (u_in->opcode) {
         case NOP:  case LOCK:  case CALLM_E:  case CALLM_S:
            break;

         /* For memory-ref instrs, copy the data_addr into a temporary
          * to be passed to the helper at the end of the instruction.
          */
         case LOAD:
            switch (u_in->size) {
               case 4:  helper = (Addr)ac_helperc_LOAD4; break;
               case 2:  helper = (Addr)ac_helperc_LOAD2; break;
               case 1:  helper = (Addr)ac_helperc_LOAD1; break;
               default: VG_(skin_panic)
                           ("addrcheck::SK_(instrument):LOAD");
            }
            uInstr1(cb, CCALL, 0, TempReg, u_in->val1);
            uCCall (cb, helper, 1, 1, False );
            VG_(copy_UInstr)(cb, u_in);
            break;

         case STORE:
            switch (u_in->size) {
               case 4:  helper = (Addr)ac_helperc_STORE4; break;
               case 2:  helper = (Addr)ac_helperc_STORE2; break;
               case 1:  helper = (Addr)ac_helperc_STORE1; break;
               default: VG_(skin_panic)
                           ("addrcheck::SK_(instrument):STORE");
            }
            uInstr1(cb, CCALL, 0, TempReg, u_in->val2);
            uCCall (cb, helper, 1, 1, False );
            VG_(copy_UInstr)(cb, u_in);
            break;

	 case SSE3ag_MemRd_RegWr:
            sk_assert(u_in->size == 4 || u_in->size == 8);
            helper = (Addr)ac_fpu_READ_check;
	    goto do_Access_ARG1;
         do_Access_ARG1:
	    sk_assert(u_in->tag1 == TempReg);
            t_addr = u_in->val1;
            t_size = newTemp(cb);
	    uInstr2(cb, MOV, 4, Literal, 0, TempReg, t_size);
	    uLiteral(cb, u_in->size);
            uInstr2(cb, CCALL, 0, TempReg, t_addr, TempReg, t_size);
            uCCall(cb, helper, 2, 2, False );
            VG_(copy_UInstr)(cb, u_in);
            break;

         case MMX2_MemRd:
            sk_assert(u_in->size == 4 || u_in->size == 8);
            helper = (Addr)ac_fpu_READ_check;
	    goto do_Access_ARG2;
         case MMX2_MemWr:
            sk_assert(u_in->size == 4 || u_in->size == 8);
            helper = (Addr)ac_fpu_WRITE_check;
	    goto do_Access_ARG2;
         case FPU_R:
            helper = (Addr)ac_fpu_READ_check;
            goto do_Access_ARG2;
         case FPU_W:
            helper = (Addr)ac_fpu_WRITE_check;
            goto do_Access_ARG2;
         do_Access_ARG2:
	    sk_assert(u_in->tag2 == TempReg);
            t_addr = u_in->val2;
            t_size = newTemp(cb);
	    uInstr2(cb, MOV, 4, Literal, 0, TempReg, t_size);
	    uLiteral(cb, u_in->size);
            uInstr2(cb, CCALL, 0, TempReg, t_addr, TempReg, t_size);
            uCCall(cb, helper, 2, 2, False );
            VG_(copy_UInstr)(cb, u_in);
            break;

         case MMX2a1_MemRd:
         case SSE3a_MemRd:
         case SSE2a_MemRd:
         case SSE3a1_MemRd:
         case SSE2a1_MemRd:
            helper = (Addr)ac_fpu_READ_check;
	    goto do_Access_ARG3;
         case SSE2a_MemWr:
	 case SSE3a_MemWr:
            helper = (Addr)ac_fpu_WRITE_check;
	    goto do_Access_ARG3;
         do_Access_ARG3:
	    sk_assert(u_in->size == 4 || u_in->size == 8
                      || u_in->size == 16 || u_in->size == 512);
            sk_assert(u_in->tag3 == TempReg);
            t_addr = u_in->val3;
            t_size = newTemp(cb);
	    uInstr2(cb, MOV, 4, Literal, 0, TempReg, t_size);
	    uLiteral(cb, u_in->size);
            uInstr2(cb, CCALL, 0, TempReg, t_addr, TempReg, t_size);
            uCCall(cb, helper, 2, 2, False );
            VG_(copy_UInstr)(cb, u_in);
            break;

         case SSE3e1_RegRd:
         case SSE3e_RegWr:
         case SSE3g1_RegWr:
         case SSE5:
         case SSE3g_RegWr:
         case SSE3e_RegRd:
         case SSE4:
         case SSE3:
         default:
            VG_(copy_UInstr)(cb, u_in);
            break;
      }
   }

   VG_(free_UCodeBlock)(cb_in);
   return cb;
}


/*------------------------------------------------------------*/
/*--- Detecting leaked (unreachable) malloc'd blocks.      ---*/
/*------------------------------------------------------------*/

/* For the memory leak detector, say whether an entire 64k chunk of
   address space is possibly in use, or not.  If in doubt return
   True.
*/
static
Bool ac_is_valid_64k_chunk ( UInt chunk_number )
{
   sk_assert(chunk_number >= 0 && chunk_number < 65536);
   if (IS_DISTINGUISHED_SM(primary_map[chunk_number])) {
      /* Definitely not in use. */
      return False;
   } else {
      return True;
   }
}


/* For the memory leak detector, say whether or not a given word
   address is to be regarded as valid. */
static
Bool ac_is_valid_address ( Addr a )
{
   UChar abits;
   sk_assert(IS_ALIGNED4_ADDR(a));
   abits = get_abits4_ALIGNED(a);
   if (abits == VGM_NIBBLE_VALID) {
      return True;
   } else {
      return False;
   }
}


/* Leak detector for this tool.  We don't actually do anything, merely
   run the generic leak detector with suitable parameters for this
   tool. */
static void ac_detect_memory_leaks ( void )
{
   MAC_(do_detect_memory_leaks) ( ac_is_valid_64k_chunk, ac_is_valid_address );
}


/* ---------------------------------------------------------------------
   Sanity check machinery (permanently engaged).
   ------------------------------------------------------------------ */

Bool SK_(cheap_sanity_check) ( void )
{
   /* nothing useful we can rapidly check */
   return True;
}

Bool SK_(expensive_sanity_check) ( void )
{
   Int i;

   /* Make sure nobody changed the distinguished secondary. */
   for (i = 0; i < 8192; i++)
      if (distinguished_secondary_map.abits[i] != VGM_BYTE_INVALID)
         return False;

   /* Make sure that the upper 3/4 of the primary map hasn't
      been messed with. */
   for (i = 65536; i < 262144; i++)
      if (primary_map[i] != & distinguished_secondary_map)
         return False;

   return True;
}
      
/*------------------------------------------------------------*/
/*--- Client requests                                      ---*/
/*------------------------------------------------------------*/

Bool SK_(handle_client_request) ( ThreadId tid, UInt* arg_block, UInt *ret )
{
#define IGNORE(what)                                                    \
   do {                                                                 \
      if (moans-- > 0) {                                                \
         VG_(message)(Vg_UserMsg,                                       \
            "Warning: Addrcheck: ignoring `%s' request.", what);     \
         VG_(message)(Vg_UserMsg,                                       \
            "   To honour this request, rerun with --tool=memcheck.");  \
      }                                                                 \
   } while (0)

   UInt* arg = arg_block;
   static Int moans = 3;

   /* Overload memcheck client reqs */
   if (!VG_IS_SKIN_USERREQ('M','C',arg[0])
    && VG_USERREQ__MALLOCLIKE_BLOCK != arg[0]
    && VG_USERREQ__FREELIKE_BLOCK   != arg[0]
    && VG_USERREQ__CREATE_MEMPOOL   != arg[0]
    && VG_USERREQ__DESTROY_MEMPOOL  != arg[0]
    && VG_USERREQ__MEMPOOL_ALLOC    != arg[0]
    && VG_USERREQ__MEMPOOL_FREE     != arg[0])
      return False;

   switch (arg[0]) {
      case VG_USERREQ__DO_LEAK_CHECK:
         ac_detect_memory_leaks();
	 *ret = 0; /* return value is meaningless */
	 break;

      /* Ignore these */
      case VG_USERREQ__CHECK_WRITABLE: /* check writable */
         IGNORE("VALGRIND_CHECK_WRITABLE");
         return False;
      case VG_USERREQ__CHECK_READABLE: /* check readable */
         IGNORE("VALGRIND_CHECK_READABLE");
         return False;
      case VG_USERREQ__MAKE_NOACCESS: /* make no access */
         IGNORE("VALGRIND_MAKE_NOACCESS");
         return False;
      case VG_USERREQ__MAKE_WRITABLE: /* make writable */
         IGNORE("VALGRIND_MAKE_WRITABLE");
         return False;
      case VG_USERREQ__MAKE_READABLE: /* make readable */
         IGNORE("VALGRIND_MAKE_READABLE");
         return False;
      case VG_USERREQ__DISCARD: /* discard */
         IGNORE("VALGRIND_CHECK_DISCARD");
         return False;

      default:
         if (MAC_(handle_common_client_requests)(tid, arg_block, ret )) {
            return True;
         } else {
            VG_(message)(Vg_UserMsg, 
                         "Warning: unknown addrcheck client request code %d",
                         arg[0]);
            return False;
         }
   }
   return True;

#undef IGNORE
}

/*------------------------------------------------------------*/
/*--- Setup                                                ---*/
/*------------------------------------------------------------*/

Bool SK_(process_cmd_line_option)(Char* arg)
{
   return MAC_(process_common_cmd_line_option)(arg);
}

void SK_(print_usage)(void)
{  
   MAC_(print_common_usage)();
}

void SK_(print_debug_usage)(void)
{  
   MAC_(print_common_debug_usage)();
}


/*------------------------------------------------------------*/
/*--- Setup                                                ---*/
/*------------------------------------------------------------*/

void SK_(pre_clo_init)(void)
{
   VG_(details_name)            ("Addrcheck");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("a fine-grained address checker");
   VG_(details_copyright_author)(
      "Copyright (C) 2002-2004, and GNU GPL'd, by Julian Seward et al.");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);
   VG_(details_avg_translation_sizeB) ( 135 );

   VG_(needs_core_errors)         ();
   VG_(needs_skin_errors)         ();
   VG_(needs_libc_freeres)        ();
   VG_(needs_command_line_options)();
   VG_(needs_client_requests)     ();
   VG_(needs_syscall_wrapper)     ();
   VG_(needs_sanity_checks)       ();
   VG_(needs_shadow_memory)       ();

   MAC_( new_mem_heap)             = & ac_new_mem_heap;
   MAC_( ban_mem_heap)             = & ac_make_noaccess;
   MAC_(copy_mem_heap)             = & ac_copy_address_range_state;
   MAC_( die_mem_heap)             = & ac_make_noaccess;
   MAC_(check_noaccess)            = & ac_check_noaccess;

   VG_(init_new_mem_startup)      ( & ac_new_mem_startup );
   VG_(init_new_mem_stack_signal) ( & ac_make_accessible );
   VG_(init_new_mem_brk)          ( & ac_make_accessible );
   VG_(init_new_mem_mmap)         ( & ac_set_perms );
   
   VG_(init_copy_mem_remap)       ( & ac_copy_address_range_state );
   VG_(init_change_mem_mprotect)  ( & ac_set_perms );
      
   VG_(init_die_mem_stack_signal) ( & ac_make_noaccess ); 
   VG_(init_die_mem_brk)          ( & ac_make_noaccess );
   VG_(init_die_mem_munmap)       ( & ac_make_noaccess ); 

   VG_(init_new_mem_stack_4)      ( & MAC_(new_mem_stack_4)  );
   VG_(init_new_mem_stack_8)      ( & MAC_(new_mem_stack_8)  );
   VG_(init_new_mem_stack_12)     ( & MAC_(new_mem_stack_12) );
   VG_(init_new_mem_stack_16)     ( & MAC_(new_mem_stack_16) );
   VG_(init_new_mem_stack_32)     ( & MAC_(new_mem_stack_32) );
   VG_(init_new_mem_stack)        ( & MAC_(new_mem_stack)    );

   VG_(init_die_mem_stack_4)      ( & MAC_(die_mem_stack_4)  );
   VG_(init_die_mem_stack_8)      ( & MAC_(die_mem_stack_8)  );
   VG_(init_die_mem_stack_12)     ( & MAC_(die_mem_stack_12) );
   VG_(init_die_mem_stack_16)     ( & MAC_(die_mem_stack_16) );
   VG_(init_die_mem_stack_32)     ( & MAC_(die_mem_stack_32) );
   VG_(init_die_mem_stack)        ( & MAC_(die_mem_stack)    );
   
   VG_(init_ban_mem_stack)        ( & ac_make_noaccess );

   VG_(init_pre_mem_read)         ( & ac_check_is_readable );
   VG_(init_pre_mem_read_asciiz)  ( & ac_check_is_readable_asciiz );
   VG_(init_pre_mem_write)        ( & ac_check_is_writable );
   VG_(init_post_mem_write)       ( & ac_make_accessible );

   VG_(register_compact_helper)((Addr) & ac_helperc_LOAD4);
   VG_(register_compact_helper)((Addr) & ac_helperc_LOAD2);
   VG_(register_compact_helper)((Addr) & ac_helperc_LOAD1);
   VG_(register_compact_helper)((Addr) & ac_helperc_STORE4);
   VG_(register_compact_helper)((Addr) & ac_helperc_STORE2);
   VG_(register_compact_helper)((Addr) & ac_helperc_STORE1);
   VG_(register_noncompact_helper)((Addr) & ac_fpu_READ_check);
   VG_(register_noncompact_helper)((Addr) & ac_fpu_WRITE_check);

   VGP_(register_profile_event) ( VgpSetMem,   "set-mem-perms" );
   VGP_(register_profile_event) ( VgpCheckMem, "check-mem-perms" );
   VGP_(register_profile_event) ( VgpESPAdj,   "adjust-ESP" );

   init_shadow_memory();
   MAC_(common_pre_clo_init)();
}

void SK_(post_clo_init) ( void )
{
}

void SK_(fini) ( Int exitcode )
{
   MAC_(common_fini)( ac_detect_memory_leaks );
}

VG_DETERMINE_INTERFACE_VERSION(SK_(pre_clo_init), 1./8)


/*--------------------------------------------------------------------*/
/*--- end                                                ac_main.c ---*/
/*--------------------------------------------------------------------*/

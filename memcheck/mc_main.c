
/*--------------------------------------------------------------------*/
/*--- MemCheck: Maintain bitmaps of memory, tracking the           ---*/
/*--- accessibility (A) and validity (V) status of each byte.      ---*/
/*---                                                    mc_main.c ---*/
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
#include "memcheck.h"   /* for client requests */
//#include "vg_profile.c"

VG_DETERMINE_INTERFACE_VERSION

/* Define to debug the mem audit system. */
/* #define VG_DEBUG_MEMORY */

/* Define to collect detailed performance info. */
/* #define VG_PROFILE_MEMORY */

#define DEBUG(fmt, args...) //VG_(printf)(fmt, ## args)

/*------------------------------------------------------------*/
/*--- Profiling events                                     ---*/
/*------------------------------------------------------------*/

typedef 
   enum { 
      VgpCheckMem = VgpFini+1,
      VgpSetMem
   } 
   VgpSkinCC;

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
   address.  Each byte is represented by nine bits, one indicating
   accessibility, the other eight validity.  So each second-level map
   contains 73728 bytes.  This two-level arrangement conveniently
   divides the 4G address space into 64k lumps, each size 64k bytes.

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
   misaligned address.  
*/


/*------------------------------------------------------------*/
/*--- Function declarations.                               ---*/
/*------------------------------------------------------------*/

static UInt mc_rd_V4_SLOWLY ( Addr a );
static UInt mc_rd_V2_SLOWLY ( Addr a );
static UInt mc_rd_V1_SLOWLY ( Addr a );
static void mc_wr_V4_SLOWLY ( Addr a, UInt vbytes );
static void mc_wr_V2_SLOWLY ( Addr a, UInt vbytes );
static void mc_wr_V1_SLOWLY ( Addr a, UInt vbytes );
static void mc_fpu_read_check_SLOWLY ( Addr addr, Int size );
static void mc_fpu_write_check_SLOWLY ( Addr addr, Int size );

/*------------------------------------------------------------*/
/*--- Data defns.                                          ---*/
/*------------------------------------------------------------*/

typedef 
   struct {
      UChar abits[8192];
      UChar vbyte[65536];
   }
   SecMap;

static SecMap* primary_map[ /*65536*/ 262144 ];
static SecMap  distinguished_secondary_map;


static void init_shadow_memory ( void )
{
   Int i;

   for (i = 0; i < 8192; i++)             /* Invalid address */
      distinguished_secondary_map.abits[i] = VGM_BYTE_INVALID; 
   for (i = 0; i < 65536; i++)            /* Invalid Value */
      distinguished_secondary_map.vbyte[i] = VGM_BYTE_INVALID; 

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

static SecMap* alloc_secondary_map ( __attribute__ ((unused)) 
                                     Char* caller )
{
   SecMap* map;
   UInt  i;
   PROF_EVENT(10);

   /* Mark all bytes as invalid access and invalid value. */

   /* It just happens that a SecMap occupies exactly 18 pages --
      although this isn't important, so the following assert is
      spurious. */
   sk_assert(0 == (sizeof(SecMap) % VKI_BYTES_PER_PAGE));
   map = VG_(get_memory_from_mmap)( sizeof(SecMap), caller );

   for (i = 0; i < 8192; i++)
      map->abits[i] = VGM_BYTE_INVALID; /* Invalid address */
   for (i = 0; i < 65536; i++)
      map->vbyte[i] = VGM_BYTE_INVALID; /* Invalid Value */

   /* VG_(printf)("ALLOC_2MAP(%s)\n", caller ); */
   return map;
}


/* Basic reading/writing of the bitmaps, for byte-sized accesses. */

static __inline__ UChar get_abit ( Addr a )
{
   SecMap* sm     = primary_map[a >> 16];
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

static __inline__ UChar get_vbyte ( Addr a )
{
   SecMap* sm     = primary_map[a >> 16];
   UInt    sm_off = a & 0xFFFF;
   PROF_EVENT(21);
#  if 0
      if (IS_DISTINGUISHED_SM(sm))
         VG_(message)(Vg_DebugMsg, 
                      "accessed distinguished 2ndary (V)map! 0x%x\n", a);
#  endif
   return sm->vbyte[sm_off];
}

static __inline__ void set_abit ( Addr a, UChar abit )
{
   SecMap* sm;
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

static __inline__ void set_vbyte ( Addr a, UChar vbyte )
{
   SecMap* sm;
   UInt    sm_off;
   PROF_EVENT(23);
   ENSURE_MAPPABLE(a, "set_vbyte");
   sm     = primary_map[a >> 16];
   sm_off = a & 0xFFFF;
   sm->vbyte[sm_off] = vbyte;
}


/* Reading/writing of the bitmaps, for aligned word-sized accesses. */

static __inline__ UChar get_abits4_ALIGNED ( Addr a )
{
   SecMap* sm;
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

static UInt __inline__ get_vbytes4_ALIGNED ( Addr a )
{
   SecMap* sm     = primary_map[a >> 16];
   UInt    sm_off = a & 0xFFFF;
   PROF_EVENT(25);
#  ifdef VG_DEBUG_MEMORY
   sk_assert(IS_ALIGNED4_ADDR(a));
#  endif
   return ((UInt*)(sm->vbyte))[sm_off >> 2];
}


/*------------------------------------------------------------*/
/*--- Setting permissions over address ranges.             ---*/
/*------------------------------------------------------------*/

static void set_address_range_perms ( Addr a, UInt len, 
                                      UInt example_a_bit,
                                      UInt example_v_bit )
{
   UChar   vbyte, abyte8;
   UInt    vword4, sm_off;
   SecMap* sm;

   PROF_EVENT(30);

   if (len == 0)
      return;

   if (len > 100 * 1000 * 1000) {
      VG_(message)(Vg_UserMsg, 
                   "Warning: set address range perms: "
                   "large range %u, a %d, v %d",
                   len, example_a_bit, example_v_bit );
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
   sk_assert(example_v_bit == VGM_BIT_VALID 
             || example_v_bit == VGM_BIT_INVALID);
   if (example_a_bit == VGM_BIT_INVALID)
      sk_assert(example_v_bit == VGM_BIT_INVALID);

   /* The validity bits to write. */
   vbyte = example_v_bit==VGM_BIT_VALID 
              ? VGM_BYTE_VALID : VGM_BYTE_INVALID;

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
   vword4 = (vbyte << 24) | (vbyte << 16) | (vbyte << 8) | vbyte;

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
      set_vbyte ( a, vbyte );
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
      ((UInt*)(sm->vbyte))[(sm_off >> 2) + 0] = vword4;
      ((UInt*)(sm->vbyte))[(sm_off >> 2) + 1] = vword4;
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
      set_vbyte ( a, vbyte );
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

void MC_(make_noaccess) ( Addr a, UInt len )
{
   PROF_EVENT(35);
   DEBUG("MC_(make_noaccess)(%p, %x)\n", a, len);
   set_address_range_perms ( a, len, VGM_BIT_INVALID, VGM_BIT_INVALID );
}

void MC_(make_writable) ( Addr a, UInt len )
{
   PROF_EVENT(36);
   DEBUG("MC_(make_writable)(%p, %x)\n", a, len);
   set_address_range_perms ( a, len, VGM_BIT_VALID, VGM_BIT_INVALID );
}

void MC_(make_readable) ( Addr a, UInt len )
{
   PROF_EVENT(37);
   DEBUG("MC_(make_readable)(%p, 0x%x)\n", a, len);
   set_address_range_perms ( a, len, VGM_BIT_VALID, VGM_BIT_VALID );
}

/* Block-copy permissions (needed for implementing realloc()). */

static void mc_copy_address_range_state ( Addr src, Addr dst, UInt len )
{
   UInt i;

   DEBUG("mc_copy_address_range_state\n");

   PROF_EVENT(40);
   for (i = 0; i < len; i++) {
      UChar abit  = get_abit ( src+i );
      UChar vbyte = get_vbyte ( src+i );
      PROF_EVENT(41);
      set_abit ( dst+i, abit );
      set_vbyte ( dst+i, vbyte );
   }
}


/* Check permissions for address range.  If inadequate permissions
   exist, *bad_addr is set to the offending address, so the caller can
   know what it is. */

Bool MC_(check_writable) ( Addr a, UInt len, Addr* bad_addr )
{
   UInt  i;
   UChar abit;
   PROF_EVENT(42);
   for (i = 0; i < len; i++) {
      PROF_EVENT(43);
      abit = get_abit(a);
      if (abit == VGM_BIT_INVALID) {
         if (bad_addr != NULL) *bad_addr = a;
         return False;
      }
      a++;
   }
   return True;
}

Bool MC_(check_readable) ( Addr a, UInt len, Addr* bad_addr )
{
   UInt  i;
   UChar abit;
   UChar vbyte;

   PROF_EVENT(44);
   DEBUG("MC_(check_readable)\n");
   for (i = 0; i < len; i++) {
      abit  = get_abit(a);
      vbyte = get_vbyte(a);
      PROF_EVENT(45);
      if (abit != VGM_BIT_VALID || vbyte != VGM_BYTE_VALID) {
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

Bool mc_check_readable_asciiz ( Addr a, Addr* bad_addr )
{
   UChar abit;
   UChar vbyte;
   PROF_EVENT(46);
   DEBUG("mc_check_readable_asciiz\n");
   while (True) {
      PROF_EVENT(47);
      abit  = get_abit(a);
      vbyte = get_vbyte(a);
      if (abit != VGM_BIT_VALID || vbyte != VGM_BYTE_VALID) {
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

/* Setting permissions for aligned words.  This supports fast stack
   operations. */

static void mc_make_noaccess_aligned ( Addr a, UInt len )
{
   SecMap* sm;
   UInt    sm_off;
   UChar   mask;
   Addr    a_past_end = a + len;

   VGP_PUSHCC(VgpSetMem);

   PROF_EVENT(50);
#  ifdef VG_DEBUG_MEMORY
   sk_assert(IS_ALIGNED4_ADDR(a));
   sk_assert(IS_ALIGNED4_ADDR(len));
#  endif

   for ( ; a < a_past_end; a += 4) {
      ENSURE_MAPPABLE(a, "mc_make_noaccess_aligned");
      sm     = primary_map[a >> 16];
      sm_off = a & 0xFFFF;
      ((UInt*)(sm->vbyte))[sm_off >> 2] = VGM_WORD_INVALID;
      mask = 0x0F;
      mask <<= (a & 4 /* 100b */);   /* a & 4 is either 0 or 4 */
      /* mask now contains 1s where we wish to make address bits
         invalid (1s). */
      sm->abits[sm_off >> 3] |= mask;
   }
   VGP_POPCC(VgpSetMem);
}

static void mc_make_writable_aligned ( Addr a, UInt len )
{
   SecMap* sm;
   UInt    sm_off;
   UChar   mask;
   Addr    a_past_end = a + len;

   VGP_PUSHCC(VgpSetMem);

   PROF_EVENT(51);
#  ifdef VG_DEBUG_MEMORY
   sk_assert(IS_ALIGNED4_ADDR(a));
   sk_assert(IS_ALIGNED4_ADDR(len));
#  endif

   for ( ; a < a_past_end; a += 4) {
      ENSURE_MAPPABLE(a, "mc_make_writable_aligned");
      sm     = primary_map[a >> 16];
      sm_off = a & 0xFFFF;
      ((UInt*)(sm->vbyte))[sm_off >> 2] = VGM_WORD_INVALID;
      mask = 0x0F;
      mask <<= (a & 4 /* 100b */);   /* a & 4 is either 0 or 4 */
      /* mask now contains 1s where we wish to make address bits
         invalid (0s). */
      sm->abits[sm_off >> 3] &= ~mask;
   }
   VGP_POPCC(VgpSetMem);
}


static
void mc_check_is_writable ( CorePart part, ThreadState* tst,
                            Char* s, Addr base, UInt size )
{
   Bool ok;
   Addr bad_addr;

   VGP_PUSHCC(VgpCheckMem);

   /* VG_(message)(Vg_DebugMsg,"check is writable: %x .. %x",
                               base,base+size-1); */
   ok = MC_(check_writable) ( base, size, &bad_addr );
   if (!ok) {
      switch (part) {
      case Vg_CoreSysCall:
         MC_(record_param_error) ( tst, bad_addr, /*isWrite =*/True, s );
         break;

      case Vg_CorePThread:
      case Vg_CoreSignal:
         MC_(record_core_mem_error)( tst, /*isWrite=*/True, s );
         break;

      default:
         VG_(skin_panic)("mc_check_is_writable: unexpected CorePart");
      }
   }

   VGP_POPCC(VgpCheckMem);
}

static
void mc_check_is_readable ( CorePart part, ThreadState* tst,
                            Char* s, Addr base, UInt size )
{     
   Bool ok;
   Addr bad_addr;

   VGP_PUSHCC(VgpCheckMem);
   
   /* VG_(message)(Vg_DebugMsg,"check is readable: %x .. %x",
                               base,base+size-1); */
   ok = MC_(check_readable) ( base, size, &bad_addr );
   if (!ok) {
      switch (part) {
      case Vg_CoreSysCall:
         MC_(record_param_error) ( tst, bad_addr, /*isWrite =*/False, s );
         break;
      
      case Vg_CorePThread:
         MC_(record_core_mem_error)( tst, /*isWrite=*/False, s );
         break;

      /* If we're being asked to jump to a silly address, record an error 
         message before potentially crashing the entire system. */
      case Vg_CoreTranslate:
         MC_(record_jump_error)( tst, bad_addr );
         break;

      default:
         VG_(skin_panic)("mc_check_is_readable: unexpected CorePart");
      }
   }
   VGP_POPCC(VgpCheckMem);
}

static
void mc_check_is_readable_asciiz ( CorePart part, ThreadState* tst,
                                   Char* s, Addr str )
{
   Bool ok = True;
   Addr bad_addr;
   /* VG_(message)(Vg_DebugMsg,"check is readable asciiz: 0x%x",str); */

   VGP_PUSHCC(VgpCheckMem);

   sk_assert(part == Vg_CoreSysCall);
   ok = mc_check_readable_asciiz ( (Addr)str, &bad_addr );
   if (!ok) {
      MC_(record_param_error) ( tst, bad_addr, /*is_writable =*/False, s );
   }

   VGP_POPCC(VgpCheckMem);
}


static
void mc_new_mem_startup( Addr a, UInt len, Bool rr, Bool ww, Bool xx )
{
   /* Ignore the permissions, just make it readable.  Seems to work... */
   DEBUG("mc_new_mem_startup(%p, %u, rr=%u, ww=%u, xx=%u)\n", a,len,rr,ww,xx);
   MC_(make_readable)(a, len);
}

static
void mc_new_mem_heap ( Addr a, UInt len, Bool is_inited )
{
   if (is_inited) {
      MC_(make_readable)(a, len);
   } else {
      MC_(make_writable)(a, len);
   }
}

static
void mc_set_perms (Addr a, UInt len, Bool rr, Bool ww, Bool xx)
{
   DEBUG("mc_set_perms(%p, %u, rr=%u ww=%u, xx=%u)\n", a, len, rr, ww, xx);
   if      (rr) MC_(make_readable)(a, len);
   else if (ww) MC_(make_writable)(a, len);
   else         MC_(make_noaccess)(a, len);
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

/* VG_(helperc_{LD,ST}V{1,2,4}) handle the common case fast.
   Under all other circumstances, it defers to the relevant _SLOWLY
   function, which can handle all situations.
*/
__attribute__ ((regparm(1)))
UInt MC_(helperc_LOADV4) ( Addr a )
{
#  ifdef VG_DEBUG_MEMORY
   return mc_rd_V4_SLOWLY(a);
#  else
   UInt    sec_no = rotateRight16(a) & 0x3FFFF;
   SecMap* sm     = primary_map[sec_no];
   UInt    a_off  = (a & 0xFFFF) >> 3;
   UChar   abits  = sm->abits[a_off];
   abits >>= (a & 4);
   abits &= 15;
   PROF_EVENT(60);
   if (abits == VGM_NIBBLE_VALID) {
      /* Handle common case quickly: a is suitably aligned, is mapped,
         and is addressible. */
      UInt v_off = a & 0xFFFF;
      return ((UInt*)(sm->vbyte))[ v_off >> 2 ];
   } else {
      /* Slow but general case. */
      return mc_rd_V4_SLOWLY(a);
   }
#  endif
}

__attribute__ ((regparm(2)))
void MC_(helperc_STOREV4) ( Addr a, UInt vbytes )
{
#  ifdef VG_DEBUG_MEMORY
   mc_wr_V4_SLOWLY(a, vbytes);
#  else
   UInt    sec_no = rotateRight16(a) & 0x3FFFF;
   SecMap* sm     = primary_map[sec_no];
   UInt    a_off  = (a & 0xFFFF) >> 3;
   UChar   abits  = sm->abits[a_off];
   abits >>= (a & 4);
   abits &= 15;
   PROF_EVENT(61);
   if (abits == VGM_NIBBLE_VALID) {
      /* Handle common case quickly: a is suitably aligned, is mapped,
         and is addressible. */
      UInt v_off = a & 0xFFFF;
      ((UInt*)(sm->vbyte))[ v_off >> 2 ] = vbytes;
   } else {
      /* Slow but general case. */
      mc_wr_V4_SLOWLY(a, vbytes);
   }
#  endif
}

__attribute__ ((regparm(1)))
UInt MC_(helperc_LOADV2) ( Addr a )
{
#  ifdef VG_DEBUG_MEMORY
   return mc_rd_V2_SLOWLY(a);
#  else
   UInt    sec_no = rotateRight16(a) & 0x1FFFF;
   SecMap* sm     = primary_map[sec_no];
   UInt    a_off  = (a & 0xFFFF) >> 3;
   PROF_EVENT(62);
   if (sm->abits[a_off] == VGM_BYTE_VALID) {
      /* Handle common case quickly. */
      UInt v_off = a & 0xFFFF;
      return 0xFFFF0000 
             |  
             (UInt)( ((UShort*)(sm->vbyte))[ v_off >> 1 ] );
   } else {
      /* Slow but general case. */
      return mc_rd_V2_SLOWLY(a);
   }
#  endif
}

__attribute__ ((regparm(2)))
void MC_(helperc_STOREV2) ( Addr a, UInt vbytes )
{
#  ifdef VG_DEBUG_MEMORY
   mc_wr_V2_SLOWLY(a, vbytes);
#  else
   UInt    sec_no = rotateRight16(a) & 0x1FFFF;
   SecMap* sm     = primary_map[sec_no];
   UInt    a_off  = (a & 0xFFFF) >> 3;
   PROF_EVENT(63);
   if (sm->abits[a_off] == VGM_BYTE_VALID) {
      /* Handle common case quickly. */
      UInt v_off = a & 0xFFFF;
      ((UShort*)(sm->vbyte))[ v_off >> 1 ] = vbytes & 0x0000FFFF;
   } else {
      /* Slow but general case. */
      mc_wr_V2_SLOWLY(a, vbytes);
   }
#  endif
}

__attribute__ ((regparm(1)))
UInt MC_(helperc_LOADV1) ( Addr a )
{
#  ifdef VG_DEBUG_MEMORY
   return mc_rd_V1_SLOWLY(a);
#  else
   UInt    sec_no = shiftRight16(a);
   SecMap* sm     = primary_map[sec_no];
   UInt    a_off  = (a & 0xFFFF) >> 3;
   PROF_EVENT(64);
   if (sm->abits[a_off] == VGM_BYTE_VALID) {
      /* Handle common case quickly. */
      UInt v_off = a & 0xFFFF;
      return 0xFFFFFF00
             |
             (UInt)( ((UChar*)(sm->vbyte))[ v_off ] );
   } else {
      /* Slow but general case. */
      return mc_rd_V1_SLOWLY(a);
   }
#  endif
}

__attribute__ ((regparm(2)))
void MC_(helperc_STOREV1) ( Addr a, UInt vbytes )
{
#  ifdef VG_DEBUG_MEMORY
   mc_wr_V1_SLOWLY(a, vbytes);
#  else
   UInt    sec_no = shiftRight16(a);
   SecMap* sm     = primary_map[sec_no];
   UInt    a_off  = (a & 0xFFFF) >> 3;
   PROF_EVENT(65);
   if (sm->abits[a_off] == VGM_BYTE_VALID) {
      /* Handle common case quickly. */
      UInt v_off = a & 0xFFFF;
      ((UChar*)(sm->vbyte))[ v_off ] = vbytes & 0x000000FF;
   } else {
      /* Slow but general case. */
      mc_wr_V1_SLOWLY(a, vbytes);
   }
#  endif
}


/*------------------------------------------------------------*/
/*--- Fallback functions to handle cases that the above    ---*/
/*--- VG_(helperc_{LD,ST}V{1,2,4}) can't manage.           ---*/
/*------------------------------------------------------------*/

static UInt mc_rd_V4_SLOWLY ( Addr a )
{
   Bool a0ok, a1ok, a2ok, a3ok;
   UInt vb0, vb1, vb2, vb3;

   PROF_EVENT(70);

   /* First establish independently the addressibility of the 4 bytes
      involved. */
   a0ok = get_abit(a+0) == VGM_BIT_VALID;
   a1ok = get_abit(a+1) == VGM_BIT_VALID;
   a2ok = get_abit(a+2) == VGM_BIT_VALID;
   a3ok = get_abit(a+3) == VGM_BIT_VALID;

   /* Also get the validity bytes for the address. */
   vb0 = (UInt)get_vbyte(a+0);
   vb1 = (UInt)get_vbyte(a+1);
   vb2 = (UInt)get_vbyte(a+2);
   vb3 = (UInt)get_vbyte(a+3);

   /* Now distinguish 3 cases */

   /* Case 1: the address is completely valid, so:
      - no addressing error
      - return V bytes as read from memory
   */
   if (a0ok && a1ok && a2ok && a3ok) {
      UInt vw = VGM_WORD_INVALID;
      vw <<= 8; vw |= vb3;
      vw <<= 8; vw |= vb2;
      vw <<= 8; vw |= vb1;
      vw <<= 8; vw |= vb0;
      return vw;
   }

   /* Case 2: the address is completely invalid.  
      - emit addressing error
      - return V word indicating validity.  
      This sounds strange, but if we make loads from invalid addresses 
      give invalid data, we also risk producing a number of confusing
      undefined-value errors later, which confuses the fact that the
      error arose in the first place from an invalid address. 
   */
   /* VG_(printf)("%p (%d %d %d %d)\n", a, a0ok, a1ok, a2ok, a3ok); */
   if (!MC_(clo_partial_loads_ok) 
       || ((a & 3) != 0)
       || (!a0ok && !a1ok && !a2ok && !a3ok)) {
      MC_(record_address_error)( a, 4, False );
      return (VGM_BYTE_VALID << 24) | (VGM_BYTE_VALID << 16) 
             | (VGM_BYTE_VALID << 8) | VGM_BYTE_VALID;
   }

   /* Case 3: the address is partially valid.  
      - no addressing error
      - returned V word is invalid where the address is invalid, 
        and contains V bytes from memory otherwise. 
      Case 3 is only allowed if MC_(clo_partial_loads_ok) is True
      (which is the default), and the address is 4-aligned.  
      If not, Case 2 will have applied.
   */
   sk_assert(MC_(clo_partial_loads_ok));
   {
      UInt vw = VGM_WORD_INVALID;
      vw <<= 8; vw |= (a3ok ? vb3 : VGM_BYTE_INVALID);
      vw <<= 8; vw |= (a2ok ? vb2 : VGM_BYTE_INVALID);
      vw <<= 8; vw |= (a1ok ? vb1 : VGM_BYTE_INVALID);
      vw <<= 8; vw |= (a0ok ? vb0 : VGM_BYTE_INVALID);
      return vw;
   }
}

static void mc_wr_V4_SLOWLY ( Addr a, UInt vbytes )
{
   /* Check the address for validity. */
   Bool aerr = False;
   PROF_EVENT(71);

   if (get_abit(a+0) != VGM_BIT_VALID) aerr = True;
   if (get_abit(a+1) != VGM_BIT_VALID) aerr = True;
   if (get_abit(a+2) != VGM_BIT_VALID) aerr = True;
   if (get_abit(a+3) != VGM_BIT_VALID) aerr = True;

   /* Store the V bytes, remembering to do it little-endian-ly. */
   set_vbyte( a+0, vbytes & 0x000000FF ); vbytes >>= 8;
   set_vbyte( a+1, vbytes & 0x000000FF ); vbytes >>= 8;
   set_vbyte( a+2, vbytes & 0x000000FF ); vbytes >>= 8;
   set_vbyte( a+3, vbytes & 0x000000FF );

   /* If an address error has happened, report it. */
   if (aerr)
      MC_(record_address_error)( a, 4, True );
}

static UInt mc_rd_V2_SLOWLY ( Addr a )
{
   /* Check the address for validity. */
   UInt vw   = VGM_WORD_INVALID;
   Bool aerr = False;
   PROF_EVENT(72);

   if (get_abit(a+0) != VGM_BIT_VALID) aerr = True;
   if (get_abit(a+1) != VGM_BIT_VALID) aerr = True;

   /* Fetch the V bytes, remembering to do it little-endian-ly. */
   vw <<= 8; vw |= (UInt)get_vbyte(a+1);
   vw <<= 8; vw |= (UInt)get_vbyte(a+0);

   /* If an address error has happened, report it. */
   if (aerr) {
      MC_(record_address_error)( a, 2, False );
      vw = (VGM_BYTE_INVALID << 24) | (VGM_BYTE_INVALID << 16) 
           | (VGM_BYTE_VALID << 8) | (VGM_BYTE_VALID);
   }
   return vw;   
}

static void mc_wr_V2_SLOWLY ( Addr a, UInt vbytes )
{
   /* Check the address for validity. */
   Bool aerr = False;
   PROF_EVENT(73);

   if (get_abit(a+0) != VGM_BIT_VALID) aerr = True;
   if (get_abit(a+1) != VGM_BIT_VALID) aerr = True;

   /* Store the V bytes, remembering to do it little-endian-ly. */
   set_vbyte( a+0, vbytes & 0x000000FF ); vbytes >>= 8;
   set_vbyte( a+1, vbytes & 0x000000FF );

   /* If an address error has happened, report it. */
   if (aerr)
      MC_(record_address_error)( a, 2, True );
}

static UInt mc_rd_V1_SLOWLY ( Addr a )
{
   /* Check the address for validity. */
   UInt vw   = VGM_WORD_INVALID;
   Bool aerr = False;
   PROF_EVENT(74);

   if (get_abit(a+0) != VGM_BIT_VALID) aerr = True;

   /* Fetch the V byte. */
   vw <<= 8; vw |= (UInt)get_vbyte(a+0);

   /* If an address error has happened, report it. */
   if (aerr) {
      MC_(record_address_error)( a, 1, False );
      vw = (VGM_BYTE_INVALID << 24) | (VGM_BYTE_INVALID << 16) 
           | (VGM_BYTE_INVALID << 8) | (VGM_BYTE_VALID);
   }
   return vw;   
}

static void mc_wr_V1_SLOWLY ( Addr a, UInt vbytes )
{
   /* Check the address for validity. */
   Bool aerr = False;
   PROF_EVENT(75);
   if (get_abit(a+0) != VGM_BIT_VALID) aerr = True;

   /* Store the V bytes, remembering to do it little-endian-ly. */
   set_vbyte( a+0, vbytes & 0x000000FF );

   /* If an address error has happened, report it. */
   if (aerr)
      MC_(record_address_error)( a, 1, True );
}


/* ---------------------------------------------------------------------
   Called from generated code, or from the assembly helpers.
   Handlers for value check failures.
   ------------------------------------------------------------------ */

void MC_(helperc_value_check0_fail) ( void )
{
   MC_(record_value_error) ( 0 );
}

void MC_(helperc_value_check1_fail) ( void )
{
   MC_(record_value_error) ( 1 );
}

void MC_(helperc_value_check2_fail) ( void )
{
   MC_(record_value_error) ( 2 );
}

void MC_(helperc_value_check4_fail) ( void )
{
   MC_(record_value_error) ( 4 );
}


/* ---------------------------------------------------------------------
   FPU load and store checks, called from generated code.
   ------------------------------------------------------------------ */

__attribute__ ((regparm(2)))
void MC_(fpu_read_check) ( Addr addr, Int size )
{
   /* Ensure the read area is both addressible and valid (ie,
      readable).  If there's an address error, don't report a value
      error too; but if there isn't an address error, check for a
      value error. 

      Try to be reasonably fast on the common case; wimp out and defer
      to mc_fpu_read_check_SLOWLY for everything else.  */

   SecMap* sm;
   UInt    sm_off, v_off, a_off;
   Addr    addr4;

   PROF_EVENT(80);

#  ifdef VG_DEBUG_MEMORY
   mc_fpu_read_check_SLOWLY ( addr, size );
#  else

   if (size == 4) {
      if (!IS_ALIGNED4_ADDR(addr)) goto slow4;
      PROF_EVENT(81);
      /* Properly aligned. */
      sm     = primary_map[addr >> 16];
      sm_off = addr & 0xFFFF;
      a_off  = sm_off >> 3;
      if (sm->abits[a_off] != VGM_BYTE_VALID) goto slow4;
      /* Properly aligned and addressible. */
      v_off = addr & 0xFFFF;
      if (((UInt*)(sm->vbyte))[ v_off >> 2 ] != VGM_WORD_VALID) 
         goto slow4;
      /* Properly aligned, addressible and with valid data. */
      return;
     slow4:
      mc_fpu_read_check_SLOWLY ( addr, 4 );
      return;
   }

   if (size == 8) {
      if (!IS_ALIGNED4_ADDR(addr)) goto slow8;
      PROF_EVENT(82);
      /* Properly aligned.  Do it in two halves. */
      addr4 = addr + 4;
      /* First half. */
      sm     = primary_map[addr >> 16];
      sm_off = addr & 0xFFFF;
      a_off  = sm_off >> 3;
      if (sm->abits[a_off] != VGM_BYTE_VALID) goto slow8;
      /* First half properly aligned and addressible. */
      v_off = addr & 0xFFFF;
      if (((UInt*)(sm->vbyte))[ v_off >> 2 ] != VGM_WORD_VALID) 
         goto slow8;
      /* Second half. */
      sm     = primary_map[addr4 >> 16];
      sm_off = addr4 & 0xFFFF;
      a_off  = sm_off >> 3;
      if (sm->abits[a_off] != VGM_BYTE_VALID) goto slow8;
      /* Second half properly aligned and addressible. */
      v_off = addr4 & 0xFFFF;
      if (((UInt*)(sm->vbyte))[ v_off >> 2 ] != VGM_WORD_VALID) 
         goto slow8;
      /* Both halves properly aligned, addressible and with valid
         data. */
      return;
     slow8:
      mc_fpu_read_check_SLOWLY ( addr, 8 );
      return;
   }

   /* Can't be bothered to huff'n'puff to make these (allegedly) rare
      cases go quickly.  */
   if (size == 2) {
      PROF_EVENT(83);
      mc_fpu_read_check_SLOWLY ( addr, 2 );
      return;
   }

   if (size == 10 || size == 28 || size == 108) {
      PROF_EVENT(84);
      mc_fpu_read_check_SLOWLY ( addr, size );
      return;
   }

   VG_(printf)("size is %d\n", size);
   VG_(skin_panic)("MC_(fpu_read_check): unhandled size");
#  endif
}


__attribute__ ((regparm(2)))
void MC_(fpu_write_check) ( Addr addr, Int size )
{
   /* Ensure the written area is addressible, and moan if otherwise.
      If it is addressible, make it valid, otherwise invalid. 
   */

   SecMap* sm;
   UInt    sm_off, v_off, a_off;
   Addr    addr4;

   PROF_EVENT(85);

#  ifdef VG_DEBUG_MEMORY
   mc_fpu_write_check_SLOWLY ( addr, size );
#  else

   if (size == 4) {
      if (!IS_ALIGNED4_ADDR(addr)) goto slow4;
      PROF_EVENT(86);
      /* Properly aligned. */
      sm     = primary_map[addr >> 16];
      sm_off = addr & 0xFFFF;
      a_off  = sm_off >> 3;
      if (sm->abits[a_off] != VGM_BYTE_VALID) goto slow4;
      /* Properly aligned and addressible.  Make valid. */
      v_off = addr & 0xFFFF;
      ((UInt*)(sm->vbyte))[ v_off >> 2 ] = VGM_WORD_VALID;
      return;
     slow4:
      mc_fpu_write_check_SLOWLY ( addr, 4 );
      return;
   }

   if (size == 8) {
      if (!IS_ALIGNED4_ADDR(addr)) goto slow8;
      PROF_EVENT(87);
      /* Properly aligned.  Do it in two halves. */
      addr4 = addr + 4;
      /* First half. */
      sm     = primary_map[addr >> 16];
      sm_off = addr & 0xFFFF;
      a_off  = sm_off >> 3;
      if (sm->abits[a_off] != VGM_BYTE_VALID) goto slow8;
      /* First half properly aligned and addressible.  Make valid. */
      v_off = addr & 0xFFFF;
      ((UInt*)(sm->vbyte))[ v_off >> 2 ] = VGM_WORD_VALID;
      /* Second half. */
      sm     = primary_map[addr4 >> 16];
      sm_off = addr4 & 0xFFFF;
      a_off  = sm_off >> 3;
      if (sm->abits[a_off] != VGM_BYTE_VALID) goto slow8;
      /* Second half properly aligned and addressible. */
      v_off = addr4 & 0xFFFF;
      ((UInt*)(sm->vbyte))[ v_off >> 2 ] = VGM_WORD_VALID;
      /* Properly aligned, addressible and with valid data. */
      return;
     slow8:
      mc_fpu_write_check_SLOWLY ( addr, 8 );
      return;
   }

   /* Can't be bothered to huff'n'puff to make these (allegedly) rare
      cases go quickly.  */
   if (size == 2) {
      PROF_EVENT(88);
      mc_fpu_write_check_SLOWLY ( addr, 2 );
      return;
   }

   if (size == 10 || size == 28 || size == 108) {
      PROF_EVENT(89);
      mc_fpu_write_check_SLOWLY ( addr, size );
      return;
   }

   VG_(printf)("size is %d\n", size);
   VG_(skin_panic)("MC_(fpu_write_check): unhandled size");
#  endif
}


/* ---------------------------------------------------------------------
   Slow, general cases for FPU load and store checks.
   ------------------------------------------------------------------ */

/* Generic version.  Test for both addr and value errors, but if
   there's an addr error, don't report a value error even if it
   exists. */

void mc_fpu_read_check_SLOWLY ( Addr addr, Int size )
{
   Int  i;
   Bool aerr = False;
   Bool verr = False;
   PROF_EVENT(90);
   for (i = 0; i < size; i++) {
      PROF_EVENT(91);
      if (get_abit(addr+i) != VGM_BIT_VALID)
         aerr = True;
      if (get_vbyte(addr+i) != VGM_BYTE_VALID)
         verr = True;
   }

   if (aerr) {
      MC_(record_address_error)( addr, size, False );
   } else {
     if (verr)
        MC_(record_value_error)( size );
   }
}


/* Generic version.  Test for addr errors.  Valid addresses are
   given valid values, and invalid addresses invalid values. */

void mc_fpu_write_check_SLOWLY ( Addr addr, Int size )
{
   Int  i;
   Addr a_here;
   Bool a_ok;
   Bool aerr = False;
   PROF_EVENT(92);
   for (i = 0; i < size; i++) {
      PROF_EVENT(93);
      a_here = addr+i;
      a_ok = get_abit(a_here) == VGM_BIT_VALID;
      if (a_ok) {
	set_vbyte(a_here, VGM_BYTE_VALID);
      } else {
	set_vbyte(a_here, VGM_BYTE_INVALID);
        aerr = True;
      }
   }
   if (aerr) {
      MC_(record_address_error)( addr, size, True );
   }
}


/*------------------------------------------------------------*/
/*--- Detecting leaked (unreachable) malloc'd blocks.      ---*/
/*------------------------------------------------------------*/

/* For the memory leak detector, say whether an entire 64k chunk of
   address space is possibly in use, or not.  If in doubt return
   True.
*/
static
Bool mc_is_valid_64k_chunk ( UInt chunk_number )
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
Bool mc_is_valid_address ( Addr a )
{
   UInt vbytes;
   UChar abits;
   sk_assert(IS_ALIGNED4_ADDR(a));
   abits  = get_abits4_ALIGNED(a);
   vbytes = get_vbytes4_ALIGNED(a);
   if (abits == VGM_NIBBLE_VALID && vbytes == VGM_WORD_VALID) {
      return True;
   } else {
      return False;
   }
}


/* Leak detector for this skin.  We don't actually do anything, merely
   run the generic leak detector with suitable parameters for this
   skin. */
void MC_(detect_memory_leaks) ( void )
{
   VG_(generic_detect_memory_leaks) ( 
      mc_is_valid_64k_chunk,
      mc_is_valid_address,
      MC_(get_where),
      MC_(clo_leak_resolution),
      MC_(clo_show_reachable)
   );
}


/* ---------------------------------------------------------------------
   Sanity check machinery (permanently engaged).
   ------------------------------------------------------------------ */

/* Check that nobody has spuriously claimed that the first or last 16
   pages (64 KB) of address space have become accessible.  Failure of
   the following do not per se indicate an internal consistency
   problem, but they are so likely to that we really want to know
   about it if so. */

Bool SK_(cheap_sanity_check) ( void )
{
   if (IS_DISTINGUISHED_SM(primary_map[0]) && 
       IS_DISTINGUISHED_SM(primary_map[65535]))
      return True;
   else
      return False;
}

Bool SK_(expensive_sanity_check) ( void )
{
   Int i;

   /* Make sure nobody changed the distinguished secondary. */
   for (i = 0; i < 8192; i++)
      if (distinguished_secondary_map.abits[i] != VGM_BYTE_INVALID)
         return False;

   for (i = 0; i < 65536; i++)
      if (distinguished_secondary_map.vbyte[i] != VGM_BYTE_INVALID)
         return False;

   /* Make sure that the upper 3/4 of the primary map hasn't
      been messed with. */
   for (i = 65536; i < 262144; i++)
      if (primary_map[i] != & distinguished_secondary_map)
         return False;

   return True;
}
      
/* ---------------------------------------------------------------------
   Debugging machinery (turn on to debug).  Something of a mess.
   ------------------------------------------------------------------ */

#if 0
/* Print the value tags on the 8 integer registers & flag reg. */

static void uint_to_bits ( UInt x, Char* str )
{
   Int i;
   Int w = 0;
   /* str must point to a space of at least 36 bytes. */
   for (i = 31; i >= 0; i--) {
      str[w++] = (x & ( ((UInt)1) << i)) ? '1' : '0';
      if (i == 24 || i == 16 || i == 8)
         str[w++] = ' ';
   }
   str[w++] = 0;
   sk_assert(w == 36);
}

/* Caution!  Not vthread-safe; looks in VG_(baseBlock), not the thread
   state table. */

static void vg_show_reg_tags ( void )
{
   Char buf1[36];
   Char buf2[36];
   UInt z_eax, z_ebx, z_ecx, z_edx, 
        z_esi, z_edi, z_ebp, z_esp, z_eflags;

   z_eax    = VG_(baseBlock)[VGOFF_(sh_eax)];
   z_ebx    = VG_(baseBlock)[VGOFF_(sh_ebx)];
   z_ecx    = VG_(baseBlock)[VGOFF_(sh_ecx)];
   z_edx    = VG_(baseBlock)[VGOFF_(sh_edx)];
   z_esi    = VG_(baseBlock)[VGOFF_(sh_esi)];
   z_edi    = VG_(baseBlock)[VGOFF_(sh_edi)];
   z_ebp    = VG_(baseBlock)[VGOFF_(sh_ebp)];
   z_esp    = VG_(baseBlock)[VGOFF_(sh_esp)];
   z_eflags = VG_(baseBlock)[VGOFF_(sh_eflags)];
   
   uint_to_bits(z_eflags, buf1);
   VG_(message)(Vg_DebugMsg, "efl %s\n", buf1);

   uint_to_bits(z_eax, buf1);
   uint_to_bits(z_ebx, buf2);
   VG_(message)(Vg_DebugMsg, "eax %s   ebx %s\n", buf1, buf2);

   uint_to_bits(z_ecx, buf1);
   uint_to_bits(z_edx, buf2);
   VG_(message)(Vg_DebugMsg, "ecx %s   edx %s\n", buf1, buf2);

   uint_to_bits(z_esi, buf1);
   uint_to_bits(z_edi, buf2);
   VG_(message)(Vg_DebugMsg, "esi %s   edi %s\n", buf1, buf2);

   uint_to_bits(z_ebp, buf1);
   uint_to_bits(z_esp, buf2);
   VG_(message)(Vg_DebugMsg, "ebp %s   esp %s\n", buf1, buf2);
}


/* For debugging only.  Scan the address space and touch all allegedly
   addressible words.  Useful for establishing where Valgrind's idea of
   addressibility has diverged from what the kernel believes. */

static 
void zzzmemscan_notify_word ( Addr a, UInt w )
{
}

void zzzmemscan ( void )
{
   Int n_notifies
      = VG_(scan_all_valid_memory)( zzzmemscan_notify_word );
   VG_(printf)("zzzmemscan: n_bytes = %d\n", 4 * n_notifies );
}
#endif




#if 0
static Int zzz = 0;

void show_bb ( Addr eip_next )
{
   VG_(printf)("[%4d] ", zzz);
   vg_show_reg_tags( &VG_(m_shadow );
   VG_(translate) ( eip_next, NULL, NULL, NULL );
}
#endif /* 0 */


/*------------------------------------------------------------*/
/*--- Setup                                                ---*/
/*------------------------------------------------------------*/

void SK_(written_shadow_regs_values)( UInt* gen_reg_value, UInt* eflags_value )
{
   *gen_reg_value = VGM_WORD_VALID;
   *eflags_value  = VGM_EFLAGS_VALID;
}

Bool SK_(process_cmd_line_option)(Char* arg)
{
#  define STREQ(s1,s2)     (0==VG_(strcmp_ws)((s1),(s2)))
#  define STREQN(nn,s1,s2) (0==VG_(strncmp_ws)((s1),(s2),(nn)))

   if (STREQ(arg, "--check-addrVs=yes"))
      MC_(clo_check_addrVs) = True;
   else if (STREQ(arg, "--check-addrVs=no"))
      MC_(clo_check_addrVs) = False;

   else if (STREQ(arg, "--avoid-strlen-errors=yes"))
      MC_(clo_avoid_strlen_errors) = True;
   else if (STREQ(arg, "--avoid-strlen-errors=no"))
      MC_(clo_avoid_strlen_errors) = False;

   else
      return MC_(process_common_cmd_line_option)(arg);

   return True;

#undef STREQ
#undef STREQN
}

Char* SK_(usage)(void)
{  
   return  
"    --partial-loads-ok=no|yes too hard to explain here; see manual [yes]\n"
"    --freelist-vol=<number>   volume of freed blocks queue [1000000]\n"
"    --leak-check=no|yes       search for memory leaks at exit? [no]\n"
"    --leak-resolution=low|med|high\n"
"                              amount of bt merging in leak check [low]\n"
"    --show-reachable=no|yes   show reachable blocks in leak check? [no]\n"
"    --workaround-gcc296-bugs=no|yes  self explanatory [no]\n"
"    --check-addrVs=no|yes     experimental lighterweight checking? [yes]\n"
"                              yes == Valgrind's original behaviour\n"
"\n"
"    --cleanup=no|yes          improve after instrumentation? [yes]\n"
"    --avoid-strlen-errors=no|yes  suppress errs from inlined strlen [yes]\n";
}


/*------------------------------------------------------------*/
/*--- Setup                                                ---*/
/*------------------------------------------------------------*/

void SK_(pre_clo_init)(void)
{
   VG_(details_name)            ("Memcheck");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("a.k.a. Valgrind, a memory error detector");
   VG_(details_copyright_author)(
      "Copyright (C) 2002, and GNU GPL'd, by Julian Seward.");
   VG_(details_bug_reports_to)  ("jseward@acm.org");
   VG_(details_avg_translation_sizeB) ( 228 );

   VG_(needs_core_errors)         ();
   VG_(needs_skin_errors)         ();
   VG_(needs_libc_freeres)        ();
   VG_(needs_sizeof_shadow_block) ( 1 );
   VG_(needs_shadow_regs)         ();
   VG_(needs_command_line_options)();
   VG_(needs_client_requests)     ();
   VG_(needs_extended_UCode)      ();
   VG_(needs_syscall_wrapper)     ();
   VG_(needs_alternative_free)    ();
   VG_(needs_sanity_checks)       ();

   VG_(track_new_mem_startup)      ( & mc_new_mem_startup );
   VG_(track_new_mem_heap)         ( & mc_new_mem_heap );
   VG_(track_new_mem_stack)        ( & MC_(make_writable) );
   VG_(track_new_mem_stack_aligned)( & mc_make_writable_aligned );
   VG_(track_new_mem_stack_signal) ( & MC_(make_writable) );
   VG_(track_new_mem_brk)          ( & MC_(make_writable) );
   VG_(track_new_mem_mmap)         ( & mc_set_perms );
   
   VG_(track_copy_mem_heap)        ( & mc_copy_address_range_state );
   VG_(track_copy_mem_remap)       ( & mc_copy_address_range_state );
   VG_(track_change_mem_mprotect)  ( & mc_set_perms );
      
   VG_(track_ban_mem_heap)         ( & MC_(make_noaccess) );
   VG_(track_ban_mem_stack)        ( & MC_(make_noaccess) );

   VG_(track_die_mem_heap)         ( & MC_(make_noaccess) );
   VG_(track_die_mem_stack)        ( & MC_(make_noaccess) );
   VG_(track_die_mem_stack_aligned)( & mc_make_noaccess_aligned ); 
   VG_(track_die_mem_stack_signal) ( & MC_(make_noaccess) ); 
   VG_(track_die_mem_brk)          ( & MC_(make_noaccess) );
   VG_(track_die_mem_munmap)       ( & MC_(make_noaccess) ); 

   VG_(track_bad_free)             ( & MC_(record_free_error) );
   VG_(track_mismatched_free)      ( & MC_(record_freemismatch_error) );

   VG_(track_pre_mem_read)         ( & mc_check_is_readable );
   VG_(track_pre_mem_read_asciiz)  ( & mc_check_is_readable_asciiz );
   VG_(track_pre_mem_write)        ( & mc_check_is_writable );
   VG_(track_post_mem_write)       ( & MC_(make_readable) );

   VG_(register_compact_helper)((Addr) & MC_(helper_value_check4_fail));
   VG_(register_compact_helper)((Addr) & MC_(helper_value_check0_fail));
   VG_(register_compact_helper)((Addr) & MC_(helper_value_check2_fail));
   VG_(register_compact_helper)((Addr) & MC_(helperc_STOREV4));
   VG_(register_compact_helper)((Addr) & MC_(helperc_STOREV1));
   VG_(register_compact_helper)((Addr) & MC_(helperc_LOADV4));
   VG_(register_compact_helper)((Addr) & MC_(helperc_LOADV1));

   /* These two made non-compact because 2-byte transactions are rare. */
   VG_(register_noncompact_helper)((Addr) & MC_(helperc_STOREV2));
   VG_(register_noncompact_helper)((Addr) & MC_(helperc_LOADV2));
   VG_(register_noncompact_helper)((Addr) & MC_(fpu_write_check));
   VG_(register_noncompact_helper)((Addr) & MC_(fpu_read_check));
   VG_(register_noncompact_helper)((Addr) & MC_(helper_value_check1_fail));

   VGP_(register_profile_event) ( VgpSetMem,   "set-mem-perms" );
   VGP_(register_profile_event) ( VgpCheckMem, "check-mem-perms" );

   init_shadow_memory();
   MC_(init_prof_mem)();
}

void SK_(post_clo_init) ( void )
{
}

void SK_(fini) ( void )
{
   VG_(print_malloc_stats)();

   if (VG_(clo_verbosity) == 1) {
      if (!MC_(clo_leak_check))
         VG_(message)(Vg_UserMsg, 
             "For a detailed leak analysis,  rerun with: --leak-check=yes");

      VG_(message)(Vg_UserMsg, 
                   "For counts of detected errors, rerun with: -v");
   }
   if (MC_(clo_leak_check)) MC_(detect_memory_leaks)();

   MC_(done_prof_mem)();

   if (0) {
      VG_(message)(Vg_DebugMsg, 
        "------ Valgrind's client block stats follow ---------------" );
      MC_(show_client_block_stats)();
   }
}

/*--------------------------------------------------------------------*/
/*--- end                                                mc_main.c ---*/
/*--------------------------------------------------------------------*/

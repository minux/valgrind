
/*--------------------------------------------------------------------*/
/*--- malloc/free wrappers for detecting errors and updating bits. ---*/
/*---                                        mac_malloc_wrappers.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of MemCheck, a heavyweight Valgrind skin for
   detecting memory errors, and AddrCheck, a lightweight Valgrind skin 
   for detecting memory errors.

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

#include "mac_shared.h"

/*------------------------------------------------------------*/
/*--- Defns                                                ---*/
/*------------------------------------------------------------*/

/* Stats ... */
static UInt cmalloc_n_mallocs  = 0;
static UInt cmalloc_n_frees    = 0;
static UInt cmalloc_bs_mallocd = 0;

/* We want a 16B redzone on heap blocks for Addrcheck and Memcheck */
UInt VG_(vg_malloc_redzone_szB) = 16;

/*------------------------------------------------------------*/
/*--- Tracking malloc'd and free'd blocks                  ---*/
/*------------------------------------------------------------*/

/* Record malloc'd blocks.  Nb: Addrcheck and Memcheck construct this
   separately in their respective initialisation functions. */
VgHashTable MAC_(malloc_list) = NULL;
   
/* Records blocks after freeing. */
static MAC_Chunk* freed_list_start  = NULL;
static MAC_Chunk* freed_list_end    = NULL;
static Int        freed_list_volume = 0;

/* Put a shadow chunk on the freed blocks queue, possibly freeing up
   some of the oldest blocks in the queue at the same time. */
static void add_to_freed_queue ( MAC_Chunk* mc )
{
   MAC_Chunk* sc1;

   /* Put it at the end of the freed list */
   if (freed_list_end == NULL) {
      sk_assert(freed_list_start == NULL);
      freed_list_end    = freed_list_start = mc;
      freed_list_volume = mc->size;
   } else {
      sk_assert(freed_list_end->next == NULL);
      freed_list_end->next = mc;
      freed_list_end       = mc;
      freed_list_volume += mc->size;
   }
   mc->next = NULL;

   /* Release enough of the oldest blocks to bring the free queue
      volume below vg_clo_freelist_vol. */

   while (freed_list_volume > MAC_(clo_freelist_vol)) {
      sk_assert(freed_list_start != NULL);
      sk_assert(freed_list_end != NULL);

      sc1 = freed_list_start;
      freed_list_volume -= sc1->size;
      /* VG_(printf)("volume now %d\n", freed_list_volume); */
      sk_assert(freed_list_volume >= 0);

      if (freed_list_start == freed_list_end) {
         freed_list_start = freed_list_end = NULL;
      } else {
         freed_list_start = sc1->next;
      }
      sc1->next = NULL; /* just paranoia */

      /* free MAC_Chunk */
      VG_(cli_free) ( (void*)(sc1->data) );
      VG_(free) ( sc1 );
   }
}

/* Return the first shadow chunk satisfying the predicate p. */
MAC_Chunk* MAC_(first_matching_freed_MAC_Chunk) ( Bool (*p)(MAC_Chunk*) )
{
   MAC_Chunk* mc;

   /* No point looking through freed blocks if we're not keeping
      them around for a while... */
   for (mc = freed_list_start; mc != NULL; mc = mc->next)
      if (p(mc))
         return mc;

   return NULL;
}

/* Allocate a user-chunk of size bytes.  Also allocate its shadow
   block, make the shadow block point at the user block.  Put the
   shadow chunk on the appropriate list, and set all memory
   protections correctly. */

static void add_MAC_Chunk ( ThreadState* tst,
                            Addr p, UInt size, MAC_AllocKind kind )
{
   MAC_Chunk* mc;

   mc            = VG_(malloc)(sizeof(MAC_Chunk));
   mc->data      = p;
   mc->size      = size;
   mc->allockind = kind;
   mc->where     = VG_(get_ExeContext)(tst);

   VG_(HT_add_node)( MAC_(malloc_list), (VgHashNode*)mc );
}

/*------------------------------------------------------------*/
/*--- client_malloc(), etc                                 ---*/
/*------------------------------------------------------------*/

/* Function pointers for the two skins to track interesting events. */
void (*MAC_(new_mem_heap)) ( Addr a, UInt len, Bool is_inited );
void (*MAC_(ban_mem_heap)) ( Addr a, UInt len );
void (*MAC_(die_mem_heap)) ( Addr a, UInt len );
void (*MAC_(copy_mem_heap))( Addr from, Addr to, UInt len );

/* Allocate memory and note change in memory available */
static __inline__
void* alloc_and_new_mem ( ThreadState* tst, UInt size, UInt alignment,
                          Bool is_zeroed, MAC_AllocKind kind )
{
   Addr p;

   VGP_PUSHCC(VgpCliMalloc);

   cmalloc_n_mallocs ++;
   cmalloc_bs_mallocd += size;

   p = (Addr)VG_(cli_malloc)(alignment, size);

   add_MAC_Chunk ( tst, p, size, kind );

   MAC_(ban_mem_heap)( p-VG_(vg_malloc_redzone_szB), 
                         VG_(vg_malloc_redzone_szB) );
   MAC_(new_mem_heap)( p, size, is_zeroed );
   MAC_(ban_mem_heap)( p+size, VG_(vg_malloc_redzone_szB) );

   VGP_POPCC(VgpCliMalloc);
   return (void*)p;
}

void* SK_(malloc) ( ThreadState* tst, Int n )
{
   if (n < 0) {
      VG_(message)(Vg_UserMsg, "Warning: silly arg (%d) to malloc()", n );
      return NULL;
   } else {
      return alloc_and_new_mem ( tst, n, VG_(clo_alignment), 
                                 /*is_zeroed*/False, MAC_AllocMalloc );
   }
}

void* SK_(__builtin_new) ( ThreadState* tst, Int n )
{
   if (n < 0) {
      VG_(message)(Vg_UserMsg, "Warning: silly arg (%d) to __builtin_new()", n);
      return NULL;
   } else {
      return alloc_and_new_mem ( tst, n, VG_(clo_alignment), 
                                 /*is_zeroed*/False, MAC_AllocNew );
   }
}

void* SK_(__builtin_vec_new) ( ThreadState* tst, Int n )
{
   if (n < 0) {
      VG_(message)(Vg_UserMsg, 
                   "Warning: silly arg (%d) to __builtin_vec_new()", n );
      return NULL;
   } else {
      return alloc_and_new_mem ( tst, n, VG_(clo_alignment), 
                                 /*is_zeroed*/False, MAC_AllocNewVec );
   }
}

void* SK_(memalign) ( ThreadState* tst, Int align, Int n )
{
   if (n < 0) {
      VG_(message)(Vg_UserMsg, "Warning: silly arg (%d) to memalign()", n);
      return NULL;
   } else {
      return alloc_and_new_mem ( tst, n, align, /*is_zeroed*/False, 
                                 MAC_AllocMalloc );
   }
}

void* SK_(calloc) ( ThreadState* tst, Int nmemb, Int size1 )
{
   void* p;
   Int   size, i;

   size = nmemb * size1;

   if (nmemb < 0 || size1 < 0) {
      VG_(message)(Vg_UserMsg, "Warning: silly args (%d,%d) to calloc()",
                               nmemb, size1 );
      return NULL;
   } else {
      p = alloc_and_new_mem ( tst, size, VG_(clo_alignment), 
                              /*is_zeroed*/True, MAC_AllocMalloc );
      for (i = 0; i < size; i++) 
         ((UChar*)p)[i] = 0;
      return p;
   }
}

static
void die_and_free_mem ( ThreadState* tst, MAC_Chunk* mc,
                        MAC_Chunk** prev_chunks_next_ptr )
{
   /* Note: ban redzones again -- just in case user de-banned them
      with a client request... */
   MAC_(ban_mem_heap)( mc->data-VG_(vg_malloc_redzone_szB), 
                                VG_(vg_malloc_redzone_szB) );
   MAC_(die_mem_heap)( mc->data, mc->size );
   MAC_(ban_mem_heap)( mc->data+mc->size, VG_(vg_malloc_redzone_szB) );

   /* Remove mc from the malloclist using prev_chunks_next_ptr to
      avoid repeating the hash table lookup.  Can't remove until at least
      after free and free_mismatch errors are done because they use
      describe_addr() which looks for it in malloclist. */
   *prev_chunks_next_ptr = mc->next;

   /* Record where freed */
   mc->where = VG_(get_ExeContext) ( tst );

   /* Put it out of harm's way for a while. */
   add_to_freed_queue ( mc );
}


static __inline__
void handle_free ( ThreadState* tst, void* p, MAC_AllocKind kind )
{
   MAC_Chunk*  mc;
   MAC_Chunk** prev_chunks_next_ptr;

   VGP_PUSHCC(VgpCliMalloc);

   cmalloc_n_frees++;

   mc = (MAC_Chunk*)VG_(HT_get_node) ( MAC_(malloc_list), (UInt)p,
                                       (VgHashNode***)&prev_chunks_next_ptr );

   if (mc == NULL) {
      MAC_(record_free_error) ( tst, (Addr)p );
      VGP_POPCC(VgpCliMalloc);
      return;
   }

   /* check if its a matching free() / delete / delete [] */
   if (kind != mc->allockind) {
      MAC_(record_freemismatch_error) ( tst, (Addr)p );
   }

   die_and_free_mem ( tst, mc, prev_chunks_next_ptr );
   VGP_POPCC(VgpCliMalloc);
}

void SK_(free) ( ThreadState* tst, void* p )
{
   handle_free(tst, p, MAC_AllocMalloc);
}

void SK_(__builtin_delete) ( ThreadState* tst, void* p )
{
   handle_free(tst, p, MAC_AllocNew);
}

void SK_(__builtin_vec_delete) ( ThreadState* tst, void* p )
{
   handle_free(tst, p, MAC_AllocNewVec);
}

void* SK_(realloc) ( ThreadState* tst, void* p, Int new_size )
{
   MAC_Chunk  *mc;
   MAC_Chunk **prev_chunks_next_ptr;
   UInt        i;

   VGP_PUSHCC(VgpCliMalloc);

   cmalloc_n_frees ++;
   cmalloc_n_mallocs ++;
   cmalloc_bs_mallocd += new_size;

   if (new_size < 0) {
      VG_(message)(Vg_UserMsg, 
                   "Warning: silly arg (%d) to realloc()", new_size );
      return NULL;
   }

   /* First try and find the block. */
   mc = (MAC_Chunk*)VG_(HT_get_node) ( MAC_(malloc_list), (UInt)p,
                                       (VgHashNode***)&prev_chunks_next_ptr );

   if (mc == NULL) {
      MAC_(record_free_error) ( tst, (Addr)p );
      /* Perhaps we should return to the program regardless. */
      VGP_POPCC(VgpCliMalloc);
      return NULL;
   }
  
   /* check if its a matching free() / delete / delete [] */
   if (MAC_AllocMalloc != mc->allockind) {
      /* can not realloc a range that was allocated with new or new [] */
      MAC_(record_freemismatch_error) ( tst, (Addr)p );
      /* but keep going anyway */
   }

   if (mc->size == new_size) {
      /* size unchanged */
      VGP_POPCC(VgpCliMalloc);
      return p;
      
   } else if (mc->size > new_size) {
      /* new size is smaller */
      MAC_(die_mem_heap)( mc->data+new_size, mc->size-new_size );
      mc->size = new_size;
      VGP_POPCC(VgpCliMalloc);
      return p;

   } else {
      /* new size is bigger */
      Addr p_new;

      /* Get new memory */
      p_new = (Addr)VG_(cli_malloc)(VG_(clo_alignment), new_size);

      /* First half kept and copied, second half new, 
         red zones as normal */
      MAC_(ban_mem_heap) ( p_new-VG_(vg_malloc_redzone_szB), 
                                 VG_(vg_malloc_redzone_szB) );
      MAC_(copy_mem_heap)( (Addr)p, p_new, mc->size );
      MAC_(new_mem_heap) ( p_new+mc->size, new_size-mc->size, /*inited*/False );
      MAC_(ban_mem_heap) ( p_new+new_size, VG_(vg_malloc_redzone_szB) );

      /* Copy from old to new */
      for (i = 0; i < mc->size; i++)
         ((UChar*)p_new)[i] = ((UChar*)p)[i];

      /* Free old memory */
      die_and_free_mem ( tst, mc, prev_chunks_next_ptr );

      /* this has to be after die_and_free_mem, otherwise the
         former succeeds in shorting out the new block, not the
         old, in the case when both are on the same list.  */
      add_MAC_Chunk ( tst, p_new, new_size, MAC_AllocMalloc );

      VGP_POPCC(VgpCliMalloc);
      return (void*)p_new;
   }  
}

void MAC_(print_malloc_stats) ( void )
{
   UInt nblocks = 0, nbytes = 0;
   
   /* Mmm... more lexical scoping */
   void count_one_chunk(VgHashNode* node) {
      MAC_Chunk* mc = (MAC_Chunk*)node;
      nblocks ++;
      nbytes  += mc->size;
   }

   if (VG_(clo_verbosity) == 0)
      return;

   /* Count memory still in use. */
   VG_(HT_apply_to_all_nodes)(MAC_(malloc_list), count_one_chunk);

   VG_(message)(Vg_UserMsg, 
                "malloc/free: in use at exit: %d bytes in %d blocks.",
                nbytes, nblocks);
   VG_(message)(Vg_UserMsg, 
                "malloc/free: %d allocs, %d frees, %u bytes allocated.",
                cmalloc_n_mallocs,
                cmalloc_n_frees, cmalloc_bs_mallocd);
   if (VG_(clo_verbosity) > 1)
      VG_(message)(Vg_UserMsg, "");
}

/*--------------------------------------------------------------------*/
/*--- end                                    mac_malloc_wrappers.c ---*/
/*--------------------------------------------------------------------*/

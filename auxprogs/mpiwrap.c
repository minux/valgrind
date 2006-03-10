
/*---------------------------------------------------------------*/
/*---                                                         ---*/
/*--- A library of wrappers for MPI 1.1 functions.            ---*/
/*---                                                         ---*/
/*---------------------------------------------------------------*/

/* ----------------------------------------------------------------

   Notice that the following BSD-style license applies to this one
   file (mpiwrap.c) only.  The rest of Valgrind is licensed under the
   terms of the GNU General Public License, version 2, unless
   otherwise indicated.  See the COPYING file in the source
   distribution for details.

   ----------------------------------------------------------------

   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2006 OpenWorks LLP.  All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

   2. The origin of this software must not be misrepresented; you must
      not claim that you wrote the original software.  If you use this
      software in a product, an acknowledgment in the product
      documentation would be appreciated but is not required.

   3. Altered source versions must be plainly marked as such, and must
      not be misrepresented as being the original software.

   4. The name of the author may not be used to endorse or promote
      products derived from this software without specific prior written
      permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
   OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   Neither the names of the U.S. Department of Energy nor the
   University of California nor the names of its contributors may be
   used to endorse or promote products derived from this software
   without prior written permission.
*/


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*------------------------------------------------------------*/


/*------------------------------------------------------------*/
/*--- includes                                             ---*/
/*------------------------------------------------------------*/

#include <stdio.h>
#include <assert.h>
#include <unistd.h>     /* getpid */
#include <stdlib.h>     /* exit */
#include <string.h>     /* strstr */
#include <pthread.h>    /* pthread_mutex_{lock,unlock} */

/* Include Valgrind magic macros for writing wrappers. */
#include "../memcheck/memcheck.h"


/*------------------------------------------------------------*/
/*--- Connect to MPI library                               ---*/
/*------------------------------------------------------------*/

/* Include headers for whatever MPI implementation the wrappers are to
   be used with.  The configure system will tell us what the path to
   the chosen MPI implementation is, via -I.. to the compiler. */
#include "mpi.h"

/* Where are API symbols?
   Open MPI  lib/libmpi.so,  soname = libmpi.so.0
   ditto Quadrics MPI
*/
/* ifdef OpenMPI ... */
#define I_WRAP_FNNAME_U(_name) I_WRAP_SONAME_FNNAME_ZU(libmpiZdsoZa,_name)


/*------------------------------------------------------------*/
/*--- Decls                                                ---*/
/*------------------------------------------------------------*/

typedef  unsigned char  Bool;
#define False ((Bool)0)
#define True  ((Bool)1)

/* Word, UWord are machine words - same size as a pointer.  This is
   checked at startup.  The wrappers below use 'long' to mean a
   machine word - this too is tested at startup. */
typedef    signed long  Word;
typedef  unsigned long  UWord;

#if !defined(offsetof)
#  define offsetof(type,memb) ((int)&((type*)0)->memb)
#endif


/*------------------------------------------------------------*/
/*--- Simple helpers                                       ---*/
/*------------------------------------------------------------*/

/* ------ Helpers for debug printing ------ */

/* constant */
static const char* preamble = "valgrind MPI wrappers";

/* established at startup */
static pid_t my_pid        = -1;
static char* options_str   = NULL;
static int   opt_verbosity = 1;
static Bool  opt_missing   = 0; /* 0:silent; 1:warn; 2:abort */
static Bool  opt_help      = False;

static inline void before ( char* fnname )
{
   /* This isn't thread-safe wrt 'done' (no locking).  It's not
      critical. */
   static int done = 0;
   if (done == 0) {
      done = 1;
      my_pid = getpid();
      options_str = getenv("MPIWRAP_DEBUG");
      if (options_str) {
         if (NULL != strstr(options_str, "warn"))
            opt_missing = 1;
         if (NULL != strstr(options_str, "strict"))
            opt_missing = 2;
         if (NULL != strstr(options_str, "verbose"))
            opt_verbosity++;
         if (NULL != strstr(options_str, "quiet"))
            opt_verbosity--;
         if (NULL != strstr(options_str, "help"))
            opt_help = True;
      }
      if (opt_verbosity > 0)
         fprintf(stderr, "%s %5d: Active for pid %d\n", 
                         preamble, my_pid, my_pid);
      /* Sanity check - that Word/UWord really are machine words. */
      assert(sizeof(Word)  == sizeof(void*));
      assert(sizeof(UWord) == sizeof(void*));
      /* Sanity check - char is byte-sized (else address calculations
         in walk_type don't work. */
      assert(sizeof(char) == 1);
      if (opt_help) {
         fprintf(stderr, "\n");
         fprintf(stderr, "Valid options for the MPIWRAP_DEBUG environment"
                         " variable are:\n");
         fprintf(stderr, "\n");
         fprintf(stderr, "   quiet      be silent except for errors\n");
         fprintf(stderr, "   verbose    show wrapper entries/exits\n");
         fprintf(stderr, "   strict     abort the program if a function"
                         " with no wrapper is used\n");
         fprintf(stderr, "   warn       give a warning if a function"
                         " with no wrapper is used\n");
         fprintf(stderr, "   help       display this message, then exit\n");
         fprintf(stderr, "\n");
         fprintf(stderr, "Multiple options are allowed, eg"
                         " MPIWRAP_DEBUG=strict,verbose\n");
         fprintf(stderr, "Note: 'warn' generates output even if 'quiet'"
                         " is also specified\n");
         fprintf(stderr, "\n");
         fprintf(stderr, "%s %5d: exiting now\n", preamble, my_pid );
         exit(1);
      }
      if (opt_verbosity > 0)
         fprintf(stderr, 
                 "%s %5d: Try MPIWRAP_DEBUG=help for possible options\n", 
                 preamble, my_pid);

   }
   if (opt_verbosity > 1)
      fprintf(stderr, "%s %5d: enter PMPI_%s\n", preamble,  my_pid, fnname );
}

static inline void after ( char* fnname, int err )
{
   if (opt_verbosity > 1)
      fprintf(stderr, "%s %5d:  exit PMPI_%s (err = %d)\n", 
                      preamble, my_pid, fnname, err );
}

static void barf ( char* msg )
{
   fprintf(stderr, "%s %5d: fatal: %s\n",   preamble, my_pid, msg);
   fprintf(stderr, "%s %5d: exiting now\n", preamble, my_pid );
   exit(1);
}

/* Half-hearted type-showing function (for debugging). */
static void showTy ( FILE* f, MPI_Datatype ty )
{
        if (ty == MPI_DATATYPE_NULL)  fprintf(f,"DATATYPE_NULL");
   else if (ty == MPI_BYTE)           fprintf(f,"BYTE");
   else if (ty == MPI_PACKED)         fprintf(f,"PACKED");
   else if (ty == MPI_CHAR)           fprintf(f,"CHAR");
   else if (ty == MPI_SHORT)          fprintf(f,"SHORT");
   else if (ty == MPI_INT)            fprintf(f,"INT");
   else if (ty == MPI_LONG)           fprintf(f,"LONG");
   else if (ty == MPI_FLOAT)          fprintf(f,"FLOAT");
   else if (ty == MPI_DOUBLE)         fprintf(f,"DOUBLE");
   else if (ty == MPI_LONG_DOUBLE)    fprintf(f,"LONG_DOUBLE");
   else if (ty == MPI_UNSIGNED_CHAR)  fprintf(f,"UNSIGNED_CHAR");
   else if (ty == MPI_UNSIGNED_SHORT) fprintf(f,"UNSIGNED_SHORT");
   else if (ty == MPI_UNSIGNED_LONG)  fprintf(f,"UNSIGNED_LONG");
   else if (ty == MPI_UNSIGNED)       fprintf(f,"UNSIGNED");
   else if (ty == MPI_FLOAT_INT)      fprintf(f,"FLOAT_INT");
   else if (ty == MPI_DOUBLE_INT)     fprintf(f,"DOUBLE_INT");
   else if (ty == MPI_LONG_DOUBLE_INT) fprintf(f,"LONG_DOUBLE_INT");
   else if (ty == MPI_LONG_INT)       fprintf(f,"LONG_INT");
   else if (ty == MPI_SHORT_INT)      fprintf(f,"SHORT_INT");
   else if (ty == MPI_2INT)           fprintf(f,"2INT");
   else if (ty == MPI_UB)             fprintf(f,"UB");
   else if (ty == MPI_LB)             fprintf(f,"LB");
#  if defined(MPI_WCHAR)
   else if (ty == MPI_WCHAR)          fprintf(f,"WCHAR");
#  endif
   else if (ty == MPI_LONG_LONG_INT)  fprintf(f,"LONG_LONG_INT");
   else if (ty == MPI_LONG_LONG)      fprintf(f,"LONG_LONG");
   else if (ty == MPI_UNSIGNED_LONG_LONG) fprintf(f,"UNSIGNED_LONG_LONG");
   else fprintf(f,"showTy:???");
}

static void showCombiner ( FILE* f, int combiner )
{
   switch (combiner) {
      case MPI_COMBINER_NAMED:       fprintf(f, "NAMED"); break;
#if   defined(MPI_COMBINER_DUP)
      case MPI_COMBINER_DUP:         fprintf(f, "DUP"); break;
#     endif
      case MPI_COMBINER_CONTIGUOUS:  fprintf(f, "CONTIGUOUS"); break;
      case MPI_COMBINER_VECTOR:      fprintf(f, "VECTOR"); break;
#if   defined(MPI_COMBINER_HVECTOR_INTEGER)
      case MPI_COMBINER_HVECTOR_INTEGER: fprintf(f, "HVECTOR_INTEGER"); break;
#     endif
      case MPI_COMBINER_HVECTOR:     fprintf(f, "HVECTOR"); break;
      case MPI_COMBINER_INDEXED:     fprintf(f, "INDEXED"); break;
#if   defined(MPI_COMBINER_HINDEXED_INTEGER)
      case MPI_COMBINER_HINDEXED_INTEGER: fprintf(f, "HINDEXED_INTEGER"); break;
#     endif
      case MPI_COMBINER_HINDEXED:    fprintf(f, "HINDEXED"); break;
#if   defined(MPI_COMBINER_INDEXED_BLOCK)
      case MPI_COMBINER_INDEXED_BLOCK: fprintf(f, "INDEXED_BLOCK"); break;
#     endif
#if   defined(MPI_COMBINER_STRUCT_INTEGER)
      case MPI_COMBINER_STRUCT_INTEGER: fprintf(f, "STRUCT_INTEGER"); break;
#     endif
      case MPI_COMBINER_STRUCT:      fprintf(f, "STRUCT"); break;
#if   defined(MPI_COMBINER_SUBARRAY)
      case MPI_COMBINER_SUBARRAY:    fprintf(f, "SUBARRAY"); break;
#     endif
#if   defined(MPI_COMBINER_DARRAY)
      case MPI_COMBINER_DARRAY:      fprintf(f, "DARRAY"); break;
#     endif
#if   defined(MPI_COMBINER_F90_REAL)
      case MPI_COMBINER_F90_REAL:    fprintf(f, "F90_REAL"); break;
#     endif
#if   defined(MPI_COMBINER_F90_COMPLEX)
      case MPI_COMBINER_F90_COMPLEX: fprintf(f, "F90_COMPLEX"); break;
#     endif
#if   defined(MPI_COMBINER_F90_INTEGER)
      case MPI_COMBINER_F90_INTEGER: fprintf(f, "F90_INTEGER"); break;
#     endif
#if   defined(MPI_COMBINER_RESIZED)
      case MPI_COMBINER_RESIZED:     fprintf(f, "RESIZED"); break;
#     endif
      default: fprintf(f, "showCombiner:??"); break;
   }
}


/* ------ Get useful bits of info ------ */

/* Note, PMPI_Comm_rank/size are themselves wrapped.  Should work
   fine. */

static inline int comm_rank ( MPI_Comm comm ) 
{
   int err, r;
   err = PMPI_Comm_rank(comm, &r);
   return err ? 0/*arbitrary*/ : r;
}

static inline int comm_size ( MPI_Comm comm ) 
{
   int err, r;
   err = PMPI_Comm_size(comm, &r);
   return err ? 0/*arbitrary*/ : r;
}

static inline Bool count_from_Status( /*OUT*/int* recv_count, 
                                      MPI_Datatype datatype, 
                                      MPI_Status* status)
{
   int n;
   int err = PMPI_Get_count(status, datatype, &n);
   if (err == MPI_SUCCESS) {
      *recv_count = n;
      return True;
   } else {
      return False;
   }
}

/* It's critical that we can do equality on MPI_Requests.
   Unfortunately these are opaque objects to us (handles, in the
   parlance of the MPI 1.1 spec).  Fortunately Sec 2.4.1 ("Opaque
   Objects") specifies that "In C, [...] These [handles] should be
   types that support assignment and equality operations."  Hence the
   following function should compile for any compliant definition of
   MPI_Request. */
static inline 
Bool eq_MPI_Request ( MPI_Request r1, MPI_Request r2 )
{
   return r1 == r2;
}

/* Get the 'extent' of a type.  Note, as per the MPI spec this
   includes whatever padding would be required when using 'ty' in an
   array. */
static long extentOfTy ( MPI_Datatype ty )
{
   int      r;
   MPI_Aint n;
   r = PMPI_Type_extent(ty, &n);
   assert(r == MPI_SUCCESS);
   return (long)n;
}

/* Free up *ty, if it is safe to do so */
static void maybeFreeTy ( MPI_Datatype* ty )
{
   int r, n_ints, n_addrs, n_dtys, tycon;

   r = PMPI_Type_get_envelope( *ty, &n_ints, &n_addrs, &n_dtys, &tycon );
   assert(r == MPI_SUCCESS);

   /* can't free named types */
   if (tycon == MPI_COMBINER_NAMED)
      return;

   /* some kinds of structs are predefined so we can't free them
      either. */
   if (*ty == MPI_FLOAT_INT || *ty == MPI_DOUBLE_INT 
       || *ty == MPI_LONG_INT || *ty == MPI_2INT 
       || *ty == MPI_SHORT_INT || *ty == MPI_LONG_DOUBLE_INT)
      return;

   /* Looks OK - free it. */
   if (0) {
      /* show me what you're about to free .. */
      fprintf(stderr, "freeing combiner ");
      showCombiner(stderr,tycon);
      fprintf(stderr, " ty= ");
      showTy(stderr,*ty);
      fprintf(stderr,"\n");
   }
   r = PMPI_Type_free(ty);
   assert(r == MPI_SUCCESS);
}

/* How big is a "named" (base) type?  Returns 0 if not known.  Note.
   There is a subtlety, which is that this is required to return the
   exact size of one item of the type, NOT the size of it when padded
   suitably to make an array of them.  In particular that's why the
   size of LONG_DOUBLE is 10 and not sizeof(long double), since the
   latter is 12 at least on x86.  Ref: MPI 1.1 doc p18 */
static long sizeofOneNamedTy ( MPI_Datatype ty )
{
   if (ty == MPI_CHAR)           return sizeof(signed char);
   if (ty == MPI_SHORT)          return sizeof(signed short int);
   if (ty == MPI_INT)            return sizeof(signed int);
   if (ty == MPI_LONG)           return sizeof(signed long int);
   if (ty == MPI_UNSIGNED_CHAR)  return sizeof(unsigned char);
   if (ty == MPI_UNSIGNED_SHORT) return sizeof(unsigned short int);
   if (ty == MPI_UNSIGNED)       return sizeof(unsigned int);
   if (ty == MPI_UNSIGNED_LONG)  return sizeof(unsigned long int);
   if (ty == MPI_FLOAT)          return sizeof(float);
   if (ty == MPI_DOUBLE)         return sizeof(double);
   if (ty == MPI_LONG_DOUBLE)    return 10; /* NOT: sizeof(long double); */
   if (ty == MPI_BYTE)           return 1;
   /* MPI_PACKED */
   /* new in MPI2: */
   if (ty == MPI_WCHAR)              return sizeof(wchar_t);
   if (ty == MPI_SIGNED_CHAR)        return sizeof(signed char);
   if (ty == MPI_UNSIGNED_LONG_LONG) return sizeof(unsigned long long int);
   if (ty == MPI_LONG_LONG_INT)      return sizeof(signed long long int);
   /* Note: the following are named structs, not named basic types,
      and so are not handled here:
         FLOAT_INT DOUBLE_INT LONG_INT 2INT SHORT_INT LONG_DOUBLE_INT
      My guess is they are probably for doing max-w-index style
      reductions, the INT carrying the index of the max/min and the
      other type its actual value.
   */
   return 0;
}


/*------------------------------------------------------------*/
/*--- Unpicking datatypes                                  ---*/
/*------------------------------------------------------------*/

static 
void walk_type_array ( void(*f)(void*,long), char* base, 
                       MPI_Datatype ty, long count );


/* Walk over all fragments of the object of type 'ty' with base
   address 'base', and apply 'f' to the start/length of each
   contiguous fragment. */
static 
void walk_type ( void(*f)(void*,long), char* base, MPI_Datatype ty )
{
   int  r, n_ints, n_addrs, n_dtys, tycon;
   long ex, i;
   int*          ints  = NULL;
   MPI_Aint*     addrs = NULL;
   MPI_Datatype* dtys  = NULL;

   if (0)
      printf("walk_type %p\n", (void*)ty);

   r = MPI_Type_get_envelope( ty, &n_ints, &n_addrs, &n_dtys, &tycon );
   assert(r == MPI_SUCCESS);

   /* Handle the base cases fast(er/ish). */
   if (tycon == MPI_COMBINER_NAMED) {
      long sz = sizeofOneNamedTy(ty);
      if (sz > 0) {
         f(base, sz);
         return;
      }
      /* Hmm.  Perhaps it's a named struct?  Unfortunately we can't
         take them to bits so we have to do a really ugly hack, which
         makes assumptions about how the MPI implementation has laid
         out these types.  At least Open MPI 1.0.1 appears to put
         the 'val' field first.
      */
      if (ty == MPI_LONG_INT) {
         typedef struct { long val; int loc; } Ty;
         f(base + offsetof(Ty,val), sizeof(long));
         f(base + offsetof(Ty,loc), sizeof(int));
         return;
      }
      if (ty == MPI_DOUBLE_INT) {
         typedef struct { double val; int loc; } Ty;
         f(base + offsetof(Ty,val), sizeof(double));
         f(base + offsetof(Ty,loc), sizeof(int));
         return;
      }
      if (ty == MPI_SHORT_INT) {
         typedef struct { short val; int loc; } Ty;
         f(base + offsetof(Ty,val), sizeof(short));
         f(base + offsetof(Ty,loc), sizeof(int));
         return;
      }
      goto unhandled;
      /*NOTREACHED*/
   }

   if (0) {
      ex = extentOfTy(ty);
      printf("tycon %p %d %d %d (ext %d)\n",
             (void*)tycon, n_ints, n_addrs, n_dtys, (int)ex );
   }

   /* Now safe to do MPI_Type_get_contents */
   assert(n_ints  >= 0);
   assert(n_addrs >= 0);
   assert(n_dtys  >= 0);

   if (n_ints  > 0) {
      ints = malloc(n_ints * sizeof(int));
      assert(ints);
   }
   if (n_addrs > 0) {
      addrs = malloc(n_addrs * sizeof(MPI_Aint));
      assert(addrs);
   }
   if (n_dtys  > 0) {
      dtys = malloc(n_dtys * sizeof(MPI_Datatype));
      assert(dtys);
   }

   r = MPI_Type_get_contents( ty, n_ints, n_addrs, n_dtys,
                                  ints, addrs, dtys );
   assert(r == MPI_SUCCESS);

   switch (tycon) {

      case MPI_COMBINER_CONTIGUOUS:
         assert(n_ints == 1 && n_addrs == 0 && n_dtys == 1);
	 walk_type_array( f, base, dtys[0], ints[0] );
         maybeFreeTy( &dtys[0] );
         break;

      case MPI_COMBINER_VECTOR:
         assert(n_ints == 3 && n_addrs == 0 && n_dtys == 1);
         ex = extentOfTy(dtys[0]);
         if (0)
         printf("vector count %d x (bl %d stride %d)\n", 
                (int)ints[0], (int)ints[1], (int)ints[2]);
         for (i = 0; i < ints[0]; i++) {
            walk_type_array( f, base + i * ints[2]/*stride*/ * ex,
                                dtys[0], ints[1]/*blocklength*/ );
         }
         maybeFreeTy( &dtys[0] );
         break;

      case MPI_COMBINER_HVECTOR:
         assert(n_ints == 2 && n_addrs == 1 && n_dtys == 1);
         ex = extentOfTy(dtys[0]);
         if (0)
         printf("hvector count %d x (bl %d hstride %d)\n", 
                (int)ints[0], (int)ints[1], (int)addrs[0]);
         for (i = 0; i < ints[0]; i++) {
            walk_type_array( f, base + i * addrs[0]/*hstride*/,
                                dtys[0], ints[1]/*blocklength*/ );
         }
         maybeFreeTy( &dtys[0] );
         break;

      case MPI_COMBINER_INDEXED:
         assert(n_addrs == 0 && n_dtys == 1);
         assert(n_ints > 0);
         assert(n_ints == 2 * ints[0] + 1);
         ex = extentOfTy(dtys[0]);
         for (i = 0; i < ints[0]; i++) {
            if (0) 
            printf("indexed (elem %d) off %d copies %d\n",
                   (int)i, ints[i+1+ints[0]], ints[i+1] );
            walk_type_array( f, base + ex * ints[i+1+ints[0]], 
                                dtys[0], ints[i+1] );
         }
         maybeFreeTy( &dtys[0] );
         break;

      case MPI_COMBINER_HINDEXED:
         assert(n_ints > 0);
         assert(n_ints == ints[0] + 1);
         assert(n_addrs == ints[0] && n_dtys == 1);
         ex = extentOfTy(dtys[0]);
         for (i = 0; i < ints[0]; i++) {
            if (0) 
            printf("hindexed (elem %d) hoff %d copies %d\n",
                   (int)i, (int)addrs[i], ints[i+1] );
            walk_type_array( f, base + addrs[i], 
                                dtys[0], ints[i+1] );
         }
         maybeFreeTy( &dtys[0] );
         break;

      case MPI_COMBINER_STRUCT:
         assert(n_addrs == n_ints-1);
         assert(n_dtys  == n_ints-1);
         assert(n_ints > 0);
         assert(n_ints == ints[0] + 1);
	 for (i = 0; i < ints[0]; i++) {
            if (0)
            printf("struct (elem %d limit %d) hoff %d copies %d\n", 
                   (int)i, (int)ints[0], (int)addrs[i], (int)ints[i+1]);
            walk_type_array( f, base + addrs[i], dtys[i], (long)ints[i+1] );
            maybeFreeTy( &dtys[i] );
	 }
         break;

      default:
         goto unhandled;

   }

   /* normal exit */
   if (ints)  free(ints);
   if (addrs) free(addrs);
   if (dtys)  free(dtys);
   return;

  unhandled:
   if (tycon == MPI_COMBINER_NAMED) {
      fprintf(stderr, "%s %5d: walk_type: unhandled base type 0x%lx ",
                      preamble, my_pid, (long)ty);
      showTy(stderr, ty);
      fprintf(stderr, "\n");
   } else {
      fprintf(stderr, "%s %5d: walk_type: unhandled combiner 0x%lx\n",
                      preamble, my_pid, (long)tycon);
   }
   if (ints)  free(ints);
   if (addrs) free(addrs);
   if (dtys)  free(dtys);
   if (opt_missing >= 2)
      barf("walk_type: unhandled combiner, strict checking selected");
}


/* Same as walk_type but apply 'f' to every element in an array of
   'count' items starting at 'base'.  The only purpose of pushing this
   into a different routine is so it can attempt to optimise the case
   where the array elements are contiguous and packed together without
   holes. */
static 
void walk_type_array ( void(*f)(void*,long), char* base, 
                       MPI_Datatype elemTy, long count )
{
   long i, ex;

   assert(sizeof(unsigned long) == sizeof(char*));

   /* First see if we can do this the fast way. */
   ex = sizeofOneNamedTy(elemTy);

   if ( /* ty is a primitive type with power-of-2 size */
        (ex == 8 || ex == 4 || ex == 2 || ex == 1)
        && /* base is suitably aligned for ty */
           ( ((unsigned long)base) & (ex-1)) == 0)  {

      /* We're sure it's contiguous, so just paint/check it in one
         go. */
     if (0) printf("walk_type_array fast %ld of size %ld\n", count, ex );
     f ( base, count * ex );

   } else {

      /* Bad news.  We have to futz with each element individually.
         This could be very expensive.

         Note: subtle.  If ty is LONG_DOUBLE then the extent will be
         12, so the following loop will jump along in steps of 12, but
         the size painted by walk_type will be 10 since it uses
         sizeofOneNamedTy to establish the size of base types.  Which
         is what we need to happen. */
      ex = extentOfTy(elemTy);
      if (0) printf("walk_type_array SLOW %ld of size %ld\n", count, ex );
      for (i = 0; i < count; i++)
         walk_type( f, base + i * ex, elemTy );

   }
}


/* Hook so it's visible from outside (can be handy to dlopen/dlsym
   it) */
void mpiwrap_walk_type_EXTERNALLY_VISIBLE
    ( void(*f)(void*,long), char* base, MPI_Datatype ty )
{
   return walk_type(f, base, ty);
}


/*------------------------------------------------------------*/
/*--- Address-range helpers                                ---*/
/*------------------------------------------------------------*/

/* ----------------
   Do corresponding checks on memory areas defined using a 
   straightforward (start, length) description.
   ----------------
*/

static inline
void check_readable_untyped ( void* buffer, long nbytes )
{
   if (nbytes > 0) {
      VALGRIND_CHECK_READABLE(buffer, nbytes);
   }
}

static inline
void check_writable_untyped ( void* buffer, long nbytes )
{
   if (nbytes > 0) {
      VALGRIND_CHECK_WRITABLE(buffer, nbytes);
   }
}

static inline
void make_defined_untyped ( void* buffer, long nbytes )
{
   if (nbytes > 0) {
      VALGRIND_MAKE_DEFINED(buffer, nbytes);
   }
}

static inline
void make_defined_if_success_untyped ( int err, 
                                       void* buffer, long nbytes )
{
   if (err == MPI_SUCCESS && nbytes > 0) {
      VALGRIND_MAKE_DEFINED(buffer, nbytes);
   }
}

/* Set the specified area to 'addressible but undefined'
   (safe-to-write) state. */

static inline
void make_writable_untyped ( void* buffer, long nbytes )
{
   if (nbytes > 0) {
      VALGRIND_MAKE_WRITABLE(buffer, nbytes);
   }
}


/* ----------------
   Do checks on memory areas defined using the MPI (buffer, count,
   type) convention.
   ----------------
*/

/* Check that the specified area is both addressible and contains
   initialised data, and cause V to complain if not. */

static
void check_readable ( char* buffer, long count, MPI_Datatype datatype )
{
   walk_type_array( check_readable_untyped, buffer, datatype, count );
}


/* Check that the specified area is addressible, and cause V to
   complain if not. Doesn't matter whether the data there is
   initialised or not. */

static
void check_writable ( void *buffer, long count, MPI_Datatype datatype )
{
   walk_type_array( check_writable_untyped, buffer, datatype, count );
}


/* Set the specified area to 'defined for each byte which is
   addressible' state. */

static
void make_defined ( void *buffer, int count, MPI_Datatype datatype )
{
   walk_type_array( make_defined_untyped, buffer, datatype, count );
}

static
void 
make_defined_if_success ( int err, void *buffer, int count, 
                                   MPI_Datatype datatype )
{
   if (err == MPI_SUCCESS)
      make_defined(buffer, count, datatype);
}


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- The wrappers proper.   They are listed in the order  ---*/
/*--- in which they appear in "MPI: A Message-Passing      ---*/
/*--- Interface Standard, MPIF, Nov 15 2003" (the MPI 1.1  ---*/
/*--- spec.  All unimplemented wrappers are listed at the  ---*/
/*--- end of the file.  The list of function names is      ---*/
/*--- taken from the headers of lampi-1.5.12.  Hopefully   ---*/
/*--- it is a complete list of all the MPI 1.1 functions.  ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/* Handy abbreviation */
#define WRAPPER_FOR(name) I_WRAP_FNNAME_U(name)

/* Generates a wrapper which aborts when called. */
#define UNIMPLEMENTED_WRAPPER(name)                            \
   void I_WRAP_FNNAME_U(PMPI_##name) ( void )                  \
   {                                                           \
      fprintf(stderr, "%s %5d: UNIMPLEMENTED wrapper: "        \
                              "PMPI_%s\n",                     \
                      preamble, my_pid, #name);                \
      fprintf(stderr, "%s %5d: exiting now.\n",                \
                      preamble, my_pid);                       \
      exit(1);                                                 \
   }

/* Generates (conceptually) a wrapper which does nothing.  In
   fact just generate no wrapper at all. */
#define NO_OP_WRAPPER(name) /* */


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 3.2, Blocking Send and Receive Operations        ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/* --- Send --- */
/* pre: rd: (buf,count,datatype) */
int WRAPPER_FOR(PMPI_Send)(void *buf, int count, MPI_Datatype datatype, 
                           int dest, int tag, MPI_Comm comm)
{
   OrigFn fn;
   int    err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Send");
   check_readable(buf, count, datatype);
   CALL_FN_W_6W(err, fn, buf,count,datatype,dest,tag,comm);
   after("Send", err);
   return err;
}

/* --- Recv --- */
/* pre:  must be writable: (buf,count,datatype)
         must be writable: status
   post: make readable: (buf,recv_count,datatype)
         where recv_count is determined from *status
*/
int WRAPPER_FOR(PMPI_Recv)(void *buf, int count, MPI_Datatype datatype, 
                           int source, int tag, 
                           MPI_Comm comm, MPI_Status *status)
{
   OrigFn fn;
   int    err, recv_count = 0;
   VALGRIND_GET_ORIG_FN(fn);
   before("Recv");
   check_writable(buf, count, datatype);
   check_writable_untyped(status, sizeof(*status));
   CALL_FN_W_7W(err, fn, buf,count,datatype,source,tag,comm,status);
   if (err == MPI_SUCCESS && count_from_Status(&recv_count,datatype,status)) {
      make_defined(buf, recv_count, datatype);
   }
   after("Recv", err);
   return err;
}


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 3.7, Nonblocking communication                   ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

typedef
   struct {
      Bool         inUse;
      MPI_Request  key;
      void*        buf;
      int          count;
      MPI_Datatype datatype;
   }
   ShadowRequest;

static ShadowRequest*  sReqs      = NULL;
static int             sReqs_size = 0;
static int             sReqs_used = 0;
static pthread_mutex_t sReqs_lock = PTHREAD_MUTEX_INITIALIZER;

#define LOCK_SREQS                                  \
  do { int pr = pthread_mutex_lock(&sReqs_lock);    \
       assert(pr == 0);                             \
  } while (0)

#define UNLOCK_SREQS                                \
  do { int pr = pthread_mutex_unlock(&sReqs_lock);  \
       assert(pr == 0);                             \
  } while (0)


/* Ensure the sReqs expandable array has at least one free slot, by
   copying it into a larger one if necessary.  NOTE: sReqs_lock is
   held throughout this procedure.*/
static void ensure_sReq_space ( void )
{
   int            i;
   ShadowRequest* sReqs2;
   if (sReqs_used == sReqs_size) {
      sReqs_size = sReqs_size==0 ? 2 : 2*sReqs_size;
      sReqs2 = malloc( sReqs_size * sizeof(ShadowRequest) );
      if (sReqs2 == NULL) {
         UNLOCK_SREQS;
         barf("add_shadow_Request: malloc failed.\n");
      }
      for (i = 0; i < sReqs_used; i++)
         sReqs2[i] = sReqs[i];
      if (sReqs)
         free(sReqs);
      sReqs = sReqs2;
   }
   assert(sReqs_used < sReqs_size);
}


/* Find shadow info for 'request', or NULL if none. */

static 
ShadowRequest* find_shadow_Request ( MPI_Request request )
{
   ShadowRequest* ret = NULL;
   int i;
   LOCK_SREQS;
   for (i = 0; i < sReqs_used; i++) {
      if (sReqs[i].inUse && eq_MPI_Request(sReqs[i].key,request)) {
         ret = &sReqs[i];
         break;
      }
   }
   UNLOCK_SREQS;
   return ret;
}


/* Delete shadow info for 'request', if any. */

static void delete_shadow_Request ( MPI_Request request )
{
   int i;
   LOCK_SREQS;
   for (i = 0; i < sReqs_used; i++) {
      if (sReqs[i].inUse && eq_MPI_Request(sReqs[i].key,request)) {
         sReqs[i].inUse = False;
         break;
      }
   }
   UNLOCK_SREQS;
}


/* Add a shadow for 'request', overwriting any old binding for it. */

static 
void add_shadow_Request( MPI_Request request, 
                         void* buf, int count, 
                         MPI_Datatype datatype )
{
   int i, ix = -1;
   LOCK_SREQS;
   assert(sReqs_used >= 0);
   assert(sReqs_size >= 0);
   assert(sReqs_used <= sReqs_size);
   if (sReqs == NULL) assert(sReqs_size == 0);

   /* First of all see if we already have a binding for this key; if
      so just replace it, and have done. */
   for (i = 0; i < sReqs_used; i++) {
      if (sReqs[i].inUse && eq_MPI_Request(sReqs[i].key,request)) {
         ix = i;
         break;
      }
   }

   if (ix < 0) {
      /* Ok, we don't have it, so will have to add it.  First search
         to see if there is an existing empty slot. */
      for (i = 0; i < sReqs_used; i++) {
         if (!sReqs[i].inUse) {
            ix = i;
            break;
         }
      }
   }

   /* No empty slots.  Allocate a new one. */
   if (ix < 0) {
      ensure_sReq_space();
      assert(sReqs_used < sReqs_size);
      ix = sReqs_used;
      sReqs_used++;
   }

   assert(ix >= 0 && ix < sReqs_used);
   assert(sReqs_used <= sReqs_size);

   sReqs[ix].inUse    = True;
   sReqs[ix].key      = request;
   sReqs[ix].buf      = buf;
   sReqs[ix].count    = count;
   sReqs[ix].datatype = datatype;

   UNLOCK_SREQS;
   if (opt_verbosity > 1)
      fprintf(stderr, "%s %5d: sReq+ 0x%lx -> b/c/d %p/%d/0x%lx [slot %d]\n",
                      preamble, my_pid, (unsigned long)request, 
                                buf, count, (long)datatype, ix);
}

#undef LOCK_SREQS
#undef UNLOCK_SREQS

static void maybe_complete ( Bool         error_in_status,
                             MPI_Request  request_before,
                             MPI_Request  request_after,
                             MPI_Status*  status )
{
   int recv_count = 0;
   ShadowRequest* shadow;
   /* How do we know if this is an Irecv request that has now
      finished successfully? 
      
      request_before isn't MPI_REQUEST_NULL
      and request_before is found in the shadow table
      and request_after *is* MPI_REQUEST_NULL
      and (if error_in_status then status.MPI_ERROR is MPI_SUCCESS)

      (when error_in_status == False, then we expect not to get
      called at all if there was an error.)
   */
   if (request_before != MPI_REQUEST_NULL
       && request_after == MPI_REQUEST_NULL
       && (error_in_status ? status->MPI_ERROR == MPI_SUCCESS : True)
       && ( (shadow=find_shadow_Request(request_before)) != NULL) ) {
      /* The Irecv detailed in 'shadow' completed.  Make the result
          buffer, and delete the entry. */
      if (count_from_Status(&recv_count, shadow->datatype, status)) {
         make_defined(shadow->buf, recv_count, shadow->datatype);
         if (opt_verbosity > 1)
            fprintf(stderr, "%s %5d: sReq- %p (completed)\n", 
                            preamble, my_pid, request_before);
      }
      delete_shadow_Request(request_before);
   }
}


static 
MPI_Request* clone_Request_array ( int count, MPI_Request* orig )
{
   MPI_Request* copy;
   int i;
   if (count < 0) 
      count = 0; /* Hmm.  Call Mulder and Scully. */
   copy = malloc( count * sizeof(MPI_Request) );
   if (copy == NULL)
      barf("clone_Request_array: malloc failed");
   for (i = 0; i < count; i++)
      copy[i] = orig[i];
   return copy;
}


/* --- Isend --- */
/* rd: (buf,count,datatype) */
/* wr: *request */
int WRAPPER_FOR(PMPI_Isend)(void *buf, int count, MPI_Datatype datatype, 
                            int dest, int tag, MPI_Comm comm, 
                            MPI_Request* request)
{
   OrigFn fn;
   int    err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Isend");
   check_readable(buf, count, datatype);
   check_writable_untyped(request, sizeof(*request));
   CALL_FN_W_7W(err, fn, buf,count,datatype,dest,tag,comm,request);
   make_defined_if_success_untyped(err, request, sizeof(*request));
   after("Isend", err);
   return err;
}

/* --- Irecv --- */
/* pre:  must be writable: (buf,count,datatype), *request
   post: make readable *request
         add a request->(buf,count,ty) binding to the 
         shadow request table. 
*/
int WRAPPER_FOR(PMPI_Irecv)( void* buf, int count, MPI_Datatype datatype, 
                             int source, int tag, MPI_Comm comm, 
                             MPI_Request* request )
{
   OrigFn fn;
   int    err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Irecv");
   check_writable(buf, count, datatype);
   check_writable_untyped(request, sizeof(*request));
   CALL_FN_W_7W(err, fn, buf,count,datatype,source,tag,comm,request);
   if (err == MPI_SUCCESS) {
      make_defined_untyped(request, sizeof(*request));
      add_shadow_Request( *request, buf,count,datatype );
   }
   after("Irecv", err);
   return err;
}

/* --- Wait --- */
int WRAPPER_FOR(PMPI_Wait)( MPI_Request* request,
                            MPI_Status* status )
{
   MPI_Request  request_before;
   OrigFn       fn;
   int          err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Wait");
   check_writable_untyped(status, sizeof(MPI_Status));
   if (*request != MPI_REQUEST_NULL)
      check_readable_untyped(request, sizeof(MPI_Request));
   request_before = *request;
   CALL_FN_W_WW(err, fn, request,status);
   if (err == MPI_SUCCESS) {
      maybe_complete(False/*err in status?*/, 
                     request_before, *request, status);
      make_defined_untyped(status, sizeof(MPI_Status));
   }
   after("Wait", err);
   return err;
}

/* --- Waitall --- */
int WRAPPER_FOR(PMPI_Waitall)( int count, 
                               MPI_Request* requests,
                               MPI_Status* statuses )
{
   MPI_Request* requests_before = NULL;
   OrigFn       fn;
   int          err, i;
   VALGRIND_GET_ORIG_FN(fn);
   before("Waitall");
   if (0) fprintf(stderr, "Waitall: %d\n", count);
   for (i = 0; i < count; i++) {
      check_writable_untyped(&statuses[i], sizeof(MPI_Status));
      if (requests[i] != MPI_REQUEST_NULL)
         check_readable_untyped(&requests[i], sizeof(MPI_Request));
   }
   requests_before = clone_Request_array( count, requests );
   CALL_FN_W_WWW(err, fn, count,requests,statuses);
   if (err == MPI_SUCCESS /*complete success*/
       || err == MPI_ERR_IN_STATUS /* partial success */) {
      Bool e_i_s = err == MPI_ERR_IN_STATUS;
      for (i = 0; i < count; i++) {
         maybe_complete(e_i_s, requests_before[i], requests[i], 
                               &statuses[i]);
         make_defined_untyped(&statuses[i], sizeof(MPI_Status));
      }
   }
   if (requests_before)
      free(requests_before);
   after("Waitall", err);
   return err;
}

/* --- Iprobe --- */
/* very unclear about this */
/* pre:  must-be-writable: *flag, *status */
/* post: make-readable *flag
         if *flag==True  make-readable *status
         if *flag==False make-uninitialised *status */
int WRAPPER_FOR(PMPI_Iprobe)(int source, int tag, 
                             MPI_Comm comm, 
                             int* flag, MPI_Status* status)
{
   OrigFn fn;
   int    err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Iprobe");
   check_writable_untyped(flag, sizeof(*flag));
   check_writable_untyped(status, sizeof(*status));
   CALL_FN_W_5W(err, fn, source,tag,comm,flag,status);
   if (err == MPI_SUCCESS) {
      make_defined_untyped(flag, sizeof(*flag));
      if (*flag)
         make_defined_untyped(status, sizeof(*status));
      else
         make_writable_untyped(status, sizeof(*status));
   }
   after("Iprobe", err);
   return err;
}


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 3.10, Send-receive                               ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/* --- Sendrecv --- */
/* pre: must be readable: (sendbuf,sendcount,sendtype)
        must be writable: (recvbuf,recvcount,recvtype)
   post: make readable: (recvbuf,recvcount_actual,datatype)
         where recvcount_actual is determined from *status
*/
int WRAPPER_FOR(PMPI_Sendrecv)(
       void *sendbuf, int sendcount, MPI_Datatype sendtype,
       int dest, int sendtag, 
       void *recvbuf, int recvcount, MPI_Datatype recvtype, 
       int source, int recvtag,
       MPI_Comm comm,  MPI_Status *status)
{
   OrigFn fn;
   int    err, recvcount_actual = 0;
   VALGRIND_GET_ORIG_FN(fn);
   before("Sendrecv");
   check_readable(sendbuf, sendcount, sendtype);
   check_writable(recvbuf, recvcount, recvtype);
   CALL_FN_W_12W(err, fn, sendbuf,sendcount,sendtype,dest,sendtag,
                          recvbuf,recvcount,recvtype,source,recvtag,
                          comm,status);
   if (err == MPI_SUCCESS 
       && count_from_Status(&recvcount_actual,recvtype,status)) {
      make_defined(recvbuf, recvcount_actual, recvtype);
   }
   after("Sendrecv", err);
   return err;
}


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 3.12, Derived datatypes                          ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/* --- Address --- */
/* Does this have anything worth checking? */


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 4.4, Broadcast                                   ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/* --- Bcast --- */
/* pre:  must-be-readable (buffer,count,datatype) for rank==root
         must-be-writable (buffer,count,datatype) for rank!=root
   post: make-readable (buffer,count,datatype) for all

   Resulting behaviour is: if root sends uninitialised stuff, then
   V complains, but then all ranks, including itself, see the buffer
   as initialised after that.
*/
int WRAPPER_FOR(PMPI_Bcast)(void *buffer, int count, 
                            MPI_Datatype datatype,
                            int root, MPI_Comm comm)
{
   OrigFn fn;
   int    err;
   Bool  i_am_sender;
   VALGRIND_GET_ORIG_FN(fn);
   before("Bcast");
   i_am_sender = root == comm_rank(comm);
   if (i_am_sender) {
      check_readable(buffer, count, datatype);
   } else {
      check_writable(buffer, count, datatype);
   }
   CALL_FN_W_5W(err, fn, buffer,count,datatype,root,comm);
   make_defined_if_success(err, buffer, count, datatype);
   after("Bcast", err);
   return err; 
}


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 4.5, Gather                                      ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/* --- Gather --- */
/* JRS 20060217: I don't really understand this.  Each process is
   going to send sendcount items of type sendtype to the root.  So
   the root is going to receive comm_size*sendcount items of type
   sendtype (right?)  So why specify recvcount and recvtype?

   Anyway, assuming the MPI Spec is correct (seems likely :-) we have:

   pre:  (all)        must be readable: (sendbuf,sendcount,sendtype)
         (root only): must be writable: (recvbuf,recvcount * comm_size,recvtype)
   post: (root only): make readable: (recvbuf,recvcount * comm_size,recvtype)
*/
int WRAPPER_FOR(PMPI_Gather)(
       void *sendbuf, int sendcount, MPI_Datatype sendtype,
       void *recvbuf, int recvcount, MPI_Datatype recvtype,
       int root, MPI_Comm comm)
{
   OrigFn fn;
   int    err, me, sz;
   VALGRIND_GET_ORIG_FN(fn);
   before("Gather");
   me = comm_rank(comm);
   sz = comm_size(comm);
   check_readable(sendbuf, sendcount, sendtype);
   if (me == root)
      check_writable(recvbuf, recvcount * sz, recvtype);
   CALL_FN_W_8W(err, fn, sendbuf,sendcount,sendtype,
                         recvbuf,recvcount,recvtype,
                         root,comm);
   make_defined_if_success(err, recvbuf, recvcount * sz, recvtype);
   after("Gather", err);
   return err;
}


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 4.9, Global Reduction Operations                 ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/* --- Reduce --- */
/* rd: (sendbuf,count,datatype) for all
   wr: (recvbuf,count,datatype) but only for rank == root
*/
int WRAPPER_FOR(PMPI_Reduce)(void *sendbuf, void *recvbuf, 
                             int count,
                             MPI_Datatype datatype, MPI_Op op, 
                             int root, MPI_Comm comm)
{
   OrigFn fn;
   int    err;
   Bool  i_am_root;
   VALGRIND_GET_ORIG_FN(fn);
   before("Reduce");
   i_am_root = root == comm_rank(comm);
   check_readable(sendbuf, count, datatype);
   if (i_am_root)
      check_writable(recvbuf, count, datatype);
   CALL_FN_W_7W(err, fn, sendbuf,recvbuf,count,datatype,op,root,comm);
   if (i_am_root)
      make_defined_if_success(err, recvbuf, count, datatype);
   after("Reduce", err);
   return err;
}


/* --- Allreduce --- */
/* rd: (sendbuf,count,datatype) for all
   wr: (recvbuf,count,datatype) for all
*/
int WRAPPER_FOR(PMPI_Allreduce)(void *sendbuf, void *recvbuf, 
                                int count,
                                MPI_Datatype datatype, MPI_Op op, 
                                MPI_Comm comm)
{
   OrigFn fn;
   int    err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Allreduce");
   check_readable(sendbuf, count, datatype);
   check_writable(recvbuf, count, datatype);
   CALL_FN_W_6W(err, fn, sendbuf,recvbuf,count,datatype,op,comm);
   make_defined_if_success(err, recvbuf, count, datatype);
   after("Allreduce", err);
   return err;
}


/* --- Op_create --- */
/* This is a bit dubious.  I suppose it takes 'function' and 
   writes something at *op, but who knows what an MPI_Op is? 
   Can we safely do 'sizeof' on it? */
int WRAPPER_FOR(PMPI_Op_create)( MPI_User_function* function,
                                 int commute, 
                                 MPI_Op* op )
{
   OrigFn fn;
   int    err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Op_create");
   check_writable_untyped(op, sizeof(*op));
   CALL_FN_W_WWW(err, fn, function,commute,op);
   make_defined_if_success_untyped(err, op, sizeof(*op));
   after("Op_create", err);
   return err;
}


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 5.4, Communicator management                     ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/* Hardly seems worth wrapping Comm_rank and Comm_size, but
   since it's done now .. */

/* --- Comm_rank --- */
/* wr: (rank, sizeof(*rank)) */
int WRAPPER_FOR(PMPI_Comm_rank)(MPI_Comm comm, int *rank)
{
   OrigFn fn;
   int    err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Comm_rank");
   check_writable_untyped(rank, sizeof(*rank));
   CALL_FN_W_WW(err, fn, comm,rank);
   make_defined_if_success_untyped(err, rank, sizeof(*rank));
   after("Comm_rank", err);
   return err;
}

/* --- Comm_size --- */
/* wr: (size, sizeof(*size)) */
int WRAPPER_FOR(PMPI_Comm_size)(MPI_Comm comm, int *size)
{
   OrigFn fn;
   int    err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Comm_size");
   check_writable_untyped(size, sizeof(*size));
   CALL_FN_W_WW(err, fn, comm,size);
   make_defined_if_success_untyped(err, size, sizeof(*size));
   after("Comm_size", err);
   return err;
}


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 5.7, Caching                                     ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 7.3, Error codes and classes                     ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/* --- Error_string --- */
int WRAPPER_FOR(PMPI_Error_string)( int errorcode, char* string, int* resultlen )
{
   OrigFn fn;
   int    err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Error_string");
   check_writable_untyped(resultlen, sizeof(int));
   check_writable_untyped(string, MPI_MAX_ERROR_STRING);
   CALL_FN_W_WWW(err, fn, errorcode,string,resultlen);
   /* Don't bother to paint the result; we assume the real function
      will have filled it with defined characters :-) */
   after("Error_string", err);
   return err;
}


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 7.5, Startup                                     ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/* --- Init --- */
/* rd: *argc, *argv[0 .. *argc-1] */
int WRAPPER_FOR(PMPI_Init)(int *argc, char ***argv)
{
   OrigFn fn;
   int    err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Init");
   check_readable_untyped(argc, sizeof(int));
   check_readable_untyped(*argv, *argc * sizeof(char**));
   CALL_FN_W_WW(err, fn, argc,argv);
   after("Init", err);
   return err;
}

/* --- Initialized --- */
int WRAPPER_FOR(PMPI_Initialized)(int* flag)
{
   OrigFn fn;
   int    err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Initialized");
   check_writable_untyped(flag, sizeof(int));
   CALL_FN_W_W(err, fn, flag);
   make_defined_if_success_untyped(err, flag, sizeof(int));
   after("Initialized", err);
   return err;
}

/* --- Finalize --- */
int WRAPPER_FOR(PMPI_Finalize)(void)
{
   OrigFn fn;
   int    err;
   VALGRIND_GET_ORIG_FN(fn);
   before("Finalize");
   CALL_FN_W_v(err, fn);
   after("Finalize", err);
   return err;
}


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Default wrappers for all remaining functions         ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

#define DEFAULT_WRAPPER_PREAMBLE(basename)                        \
      OrigFn fn;                                                  \
      UWord  res;                                                 \
      static int complaints = 3;                                  \
      VALGRIND_GET_ORIG_FN(fn);                                   \
      before(#basename);                                          \
      if (opt_missing >= 2) {                                     \
         barf("no wrapper for PMPI_" #basename                    \
              ",\n\t\t\t     and you have "                       \
              "requested strict checking");                       \
      }                                                           \
      if (opt_missing == 1 && complaints > 0) {                   \
         fprintf(stderr, "%s %5d: warning: no wrapper "           \
                         "for PMPI_" #basename "\n",              \
                 preamble, my_pid);                               \
         complaints--;                                            \
      }                                                           \

#define DEFAULT_WRAPPER_W_0W(basename)                            \
   UWord WRAPPER_FOR(PMPI_##basename)( void )                     \
   {                                                              \
      DEFAULT_WRAPPER_PREAMBLE(basename)                          \
      CALL_FN_W_v(res, fn);                                       \
      return res;                                                 \
   }

#define DEFAULT_WRAPPER_W_1W(basename)                            \
   UWord WRAPPER_FOR(PMPI_##basename)( UWord a1 )                 \
   {                                                              \
      DEFAULT_WRAPPER_PREAMBLE(basename)                          \
      CALL_FN_W_W(res, fn, a1);                                   \
      return res;                                                 \
   }

#define DEFAULT_WRAPPER_W_2W(basename)                            \
   UWord WRAPPER_FOR(PMPI_##basename)( UWord a1, UWord a2 )       \
   {                                                              \
      DEFAULT_WRAPPER_PREAMBLE(basename)                          \
      CALL_FN_W_WW(res, fn, a1,a2);                               \
      return res;                                                 \
   }

#define DEFAULT_WRAPPER_W_3W(basename)                            \
   UWord WRAPPER_FOR(PMPI_##basename)                             \
      ( UWord a1, UWord a2, UWord a3 )                            \
   {                                                              \
      DEFAULT_WRAPPER_PREAMBLE(basename)                          \
      CALL_FN_W_WWW(res, fn, a1,a2,a3);                           \
      return res;                                                 \
   }

#define DEFAULT_WRAPPER_W_4W(basename)                            \
   UWord WRAPPER_FOR(PMPI_##basename)                             \
      ( UWord a1, UWord a2, UWord a3, UWord a4 )                  \
   {                                                              \
      DEFAULT_WRAPPER_PREAMBLE(basename)                          \
      CALL_FN_W_WWWW(res, fn, a1,a2,a3,a4);                       \
      return res;                                                 \
   }

#define DEFAULT_WRAPPER_W_5W(basename)                            \
   UWord WRAPPER_FOR(PMPI_##basename)                             \
      ( UWord a1, UWord a2, UWord a3, UWord a4, UWord a5 )        \
   {                                                              \
      DEFAULT_WRAPPER_PREAMBLE(basename)                          \
      CALL_FN_W_5W(res, fn, a1,a2,a3,a4,a5);                      \
      return res;                                                 \
   }

#define DEFAULT_WRAPPER_W_6W(basename)                            \
   UWord WRAPPER_FOR(PMPI_##basename)                             \
      ( UWord a1, UWord a2, UWord a3, UWord a4, UWord a5,         \
        UWord a6 )                                                \
   {                                                              \
      DEFAULT_WRAPPER_PREAMBLE(basename)                          \
      CALL_FN_W_6W(res, fn, a1,a2,a3,a4,a5,a6);                   \
      return res;                                                 \
   }

#define DEFAULT_WRAPPER_W_7W(basename)                            \
   UWord WRAPPER_FOR(PMPI_##basename)                             \
      ( UWord a1, UWord a2, UWord a3, UWord a4, UWord a5,         \
        UWord a6, UWord a7 )                                      \
   {                                                              \
      DEFAULT_WRAPPER_PREAMBLE(basename)                          \
      CALL_FN_W_7W(res, fn, a1,a2,a3,a4,a5,a6,a7);                \
      return res;                                                 \
   }

#define DEFAULT_WRAPPER_W_8W(basename)                            \
   UWord WRAPPER_FOR(PMPI_##basename)                             \
      ( UWord a1, UWord a2, UWord a3, UWord a4, UWord a5,         \
        UWord a6, UWord a7, UWord a8 )                            \
   {                                                              \
      DEFAULT_WRAPPER_PREAMBLE(basename)                          \
      CALL_FN_W_8W(res, fn, a1,a2,a3,a4,a5,a6,a7,a8);             \
      return res;                                                 \
   }

#define DEFAULT_WRAPPER_W_9W(basename)                            \
   UWord WRAPPER_FOR(PMPI_##basename)                             \
      ( UWord a1, UWord a2, UWord a3, UWord a4, UWord a5,         \
        UWord a6, UWord a7, UWord a8, UWord a9 )                  \
   {                                                              \
      DEFAULT_WRAPPER_PREAMBLE(basename)                          \
      CALL_FN_W_9W(res, fn, a1,a2,a3,a4,a5,a6,a7,a8,a9);          \
      return res;                                                 \
   }

#define DEFAULT_WRAPPER_W_10W(basename)                           \
   UWord WRAPPER_FOR(PMPI_##basename)                             \
      ( UWord a1, UWord a2, UWord a3, UWord a4, UWord a5,         \
        UWord a6, UWord a7, UWord a8, UWord a9, UWord a10 )       \
   {                                                              \
      DEFAULT_WRAPPER_PREAMBLE(basename)                          \
      CALL_FN_W_10W(res, fn, a1,a2,a3,a4,a5,a6,a7,a8,a9,a10);     \
      return res;                                                 \
   }

#define DEFAULT_WRAPPER_W_12W(basename)                           \
   UWord WRAPPER_FOR(PMPI_##basename)                             \
      ( UWord a1, UWord a2, UWord a3, UWord a4, UWord a5,         \
        UWord a6, UWord a7, UWord a8, UWord a9, UWord a10,        \
        UWord a11, UWord a12 )                                    \
   {                                                              \
      DEFAULT_WRAPPER_PREAMBLE(basename)                          \
      CALL_FN_W_12W(res, fn, a1,a2,a3,a4,a5,a6,                   \
                             a7,a8,a9,a10,a11,a12);               \
      return res;                                                 \
   }


/* Canned summary of MPI-1.1/MPI-2 entry points, as derived from mpi.h
   from Open MPI svn rev 9191 (somewhere between Open MPI versions
   1.0.1 and 1.1.0). */

/* If a function is commented out in this list, it's because it has a
   proper wrapper written elsewhere (above here). */

DEFAULT_WRAPPER_W_2W(Abort)
DEFAULT_WRAPPER_W_9W(Accumulate)
DEFAULT_WRAPPER_W_1W(Add_error_class)
DEFAULT_WRAPPER_W_2W(Add_error_code)
DEFAULT_WRAPPER_W_2W(Add_error_string)
DEFAULT_WRAPPER_W_2W(Address)
DEFAULT_WRAPPER_W_7W(Allgather)
DEFAULT_WRAPPER_W_8W(Allgatherv)
DEFAULT_WRAPPER_W_3W(Alloc_mem)
/* DEFAULT_WRAPPER_W_6W(Allreduce) */
DEFAULT_WRAPPER_W_7W(Alltoall)
DEFAULT_WRAPPER_W_9W(Alltoallv)
DEFAULT_WRAPPER_W_9W(Alltoallw)
DEFAULT_WRAPPER_W_2W(Attr_delete)
DEFAULT_WRAPPER_W_4W(Attr_get)
DEFAULT_WRAPPER_W_3W(Attr_put)
DEFAULT_WRAPPER_W_1W(Barrier)
/* DEFAULT_WRAPPER_W_5W(Bcast) */
DEFAULT_WRAPPER_W_6W(Bsend)
DEFAULT_WRAPPER_W_7W(Bsend_init)
DEFAULT_WRAPPER_W_2W(Buffer_attach)
DEFAULT_WRAPPER_W_2W(Buffer_detach)
DEFAULT_WRAPPER_W_1W(Cancel)
DEFAULT_WRAPPER_W_4W(Cart_coords)
DEFAULT_WRAPPER_W_6W(Cart_create)
DEFAULT_WRAPPER_W_5W(Cart_get)
DEFAULT_WRAPPER_W_5W(Cart_map)
DEFAULT_WRAPPER_W_3W(Cart_rank)
DEFAULT_WRAPPER_W_5W(Cart_shift)
DEFAULT_WRAPPER_W_3W(Cart_sub)
DEFAULT_WRAPPER_W_2W(Cartdim_get)
DEFAULT_WRAPPER_W_1W(Close_port)
DEFAULT_WRAPPER_W_5W(Comm_accept)
DEFAULT_WRAPPER_W_1W(Comm_c2f)
DEFAULT_WRAPPER_W_2W(Comm_call_errhandler)
DEFAULT_WRAPPER_W_3W(Comm_compare)
DEFAULT_WRAPPER_W_5W(Comm_connect)
DEFAULT_WRAPPER_W_2W(Comm_create_errhandler)
DEFAULT_WRAPPER_W_4W(Comm_create_keyval)
DEFAULT_WRAPPER_W_3W(Comm_create)
DEFAULT_WRAPPER_W_2W(Comm_delete_attr)
DEFAULT_WRAPPER_W_1W(Comm_disconnect)
DEFAULT_WRAPPER_W_2W(Comm_dup)
DEFAULT_WRAPPER_W_1W(Comm_f2c)
DEFAULT_WRAPPER_W_1W(Comm_free_keyval)
DEFAULT_WRAPPER_W_1W(Comm_free)
DEFAULT_WRAPPER_W_4W(Comm_get_attr)
DEFAULT_WRAPPER_W_2W(Comm_get_errhandler)
DEFAULT_WRAPPER_W_3W(Comm_get_name)
DEFAULT_WRAPPER_W_1W(Comm_get_parent)
DEFAULT_WRAPPER_W_2W(Comm_group)
DEFAULT_WRAPPER_W_2W(Comm_join)
/* DEFAULT_WRAPPER_W_2W(Comm_rank) */
DEFAULT_WRAPPER_W_2W(Comm_remote_group)
DEFAULT_WRAPPER_W_2W(Comm_remote_size)
DEFAULT_WRAPPER_W_3W(Comm_set_attr)
DEFAULT_WRAPPER_W_2W(Comm_set_errhandler)
DEFAULT_WRAPPER_W_2W(Comm_set_name)
/* DEFAULT_WRAPPER_W_2W(Comm_size) */
DEFAULT_WRAPPER_W_8W(Comm_spawn)
DEFAULT_WRAPPER_W_9W(Comm_spawn_multiple)
DEFAULT_WRAPPER_W_4W(Comm_split)
DEFAULT_WRAPPER_W_2W(Comm_test_inter)
DEFAULT_WRAPPER_W_3W(Dims_create)
DEFAULT_WRAPPER_W_1W(Errhandler_c2f)
DEFAULT_WRAPPER_W_2W(Errhandler_create)
DEFAULT_WRAPPER_W_1W(Errhandler_f2c)
DEFAULT_WRAPPER_W_1W(Errhandler_free)
DEFAULT_WRAPPER_W_2W(Errhandler_get)
DEFAULT_WRAPPER_W_2W(Errhandler_set)
DEFAULT_WRAPPER_W_2W(Error_class)
/* DEFAULT_WRAPPER_W_3W(Error_string) */
DEFAULT_WRAPPER_W_6W(Exscan)
DEFAULT_WRAPPER_W_1W(File_c2f)
DEFAULT_WRAPPER_W_1W(File_f2c)
DEFAULT_WRAPPER_W_2W(File_call_errhandler)
DEFAULT_WRAPPER_W_2W(File_create_errhandler)
DEFAULT_WRAPPER_W_2W(File_set_errhandler)
DEFAULT_WRAPPER_W_2W(File_get_errhandler)
DEFAULT_WRAPPER_W_5W(File_open)
DEFAULT_WRAPPER_W_1W(File_close)
DEFAULT_WRAPPER_W_2W(File_delete)
DEFAULT_WRAPPER_W_2W(File_set_size)
DEFAULT_WRAPPER_W_2W(File_preallocate)
DEFAULT_WRAPPER_W_2W(File_get_size)
DEFAULT_WRAPPER_W_2W(File_get_group)
DEFAULT_WRAPPER_W_2W(File_get_amode)
DEFAULT_WRAPPER_W_2W(File_set_info)
DEFAULT_WRAPPER_W_2W(File_get_info)
DEFAULT_WRAPPER_W_6W(File_set_view)
DEFAULT_WRAPPER_W_5W(File_get_view)
DEFAULT_WRAPPER_W_6W(File_read_at)
DEFAULT_WRAPPER_W_6W(File_read_at_all)
DEFAULT_WRAPPER_W_6W(File_write_at)
DEFAULT_WRAPPER_W_6W(File_write_at_all)
DEFAULT_WRAPPER_W_6W(File_iread_at)
DEFAULT_WRAPPER_W_6W(File_iwrite_at)
DEFAULT_WRAPPER_W_5W(File_read)
DEFAULT_WRAPPER_W_5W(File_read_all)
DEFAULT_WRAPPER_W_5W(File_write)
DEFAULT_WRAPPER_W_5W(File_write_all)
DEFAULT_WRAPPER_W_5W(File_iread)
DEFAULT_WRAPPER_W_5W(File_iwrite)
DEFAULT_WRAPPER_W_3W(File_seek)
DEFAULT_WRAPPER_W_2W(File_get_position)
DEFAULT_WRAPPER_W_3W(File_get_byte_offset)
DEFAULT_WRAPPER_W_5W(File_read_shared)
DEFAULT_WRAPPER_W_5W(File_write_shared)
DEFAULT_WRAPPER_W_5W(File_iread_shared)
DEFAULT_WRAPPER_W_5W(File_iwrite_shared)
DEFAULT_WRAPPER_W_5W(File_read_ordered)
DEFAULT_WRAPPER_W_5W(File_write_ordered)
DEFAULT_WRAPPER_W_3W(File_seek_shared)
DEFAULT_WRAPPER_W_2W(File_get_position_shared)
DEFAULT_WRAPPER_W_5W(File_read_at_all_begin)
DEFAULT_WRAPPER_W_3W(File_read_at_all_end)
DEFAULT_WRAPPER_W_5W(File_write_at_all_begin)
DEFAULT_WRAPPER_W_3W(File_write_at_all_end)
DEFAULT_WRAPPER_W_4W(File_read_all_begin)
DEFAULT_WRAPPER_W_3W(File_read_all_end)
DEFAULT_WRAPPER_W_4W(File_write_all_begin)
DEFAULT_WRAPPER_W_3W(File_write_all_end)
DEFAULT_WRAPPER_W_4W(File_read_ordered_begin)
DEFAULT_WRAPPER_W_3W(File_read_ordered_end)
DEFAULT_WRAPPER_W_4W(File_write_ordered_begin)
DEFAULT_WRAPPER_W_3W(File_write_ordered_end)
DEFAULT_WRAPPER_W_3W(File_get_type_extent)
DEFAULT_WRAPPER_W_2W(File_set_atomicity)
DEFAULT_WRAPPER_W_2W(File_get_atomicity)
DEFAULT_WRAPPER_W_1W(File_sync)
/* DEFAULT_WRAPPER_W_0W(Finalize) */
DEFAULT_WRAPPER_W_1W(Finalized)
DEFAULT_WRAPPER_W_1W(Free_mem)
/* DEFAULT_WRAPPER_W_8W(Gather) */
DEFAULT_WRAPPER_W_9W(Gatherv)
DEFAULT_WRAPPER_W_2W(Get_address)
DEFAULT_WRAPPER_W_3W(Get_count)
DEFAULT_WRAPPER_W_3W(Get_elements)
DEFAULT_WRAPPER_W_8W(Get)
DEFAULT_WRAPPER_W_2W(Get_processor_name)
DEFAULT_WRAPPER_W_2W(Get_version)
DEFAULT_WRAPPER_W_6W(Graph_create)
DEFAULT_WRAPPER_W_5W(Graph_get)
DEFAULT_WRAPPER_W_5W(Graph_map)
DEFAULT_WRAPPER_W_3W(Graph_neighbors_count)
DEFAULT_WRAPPER_W_4W(Graph_neighbors)
DEFAULT_WRAPPER_W_3W(Graphdims_get)
DEFAULT_WRAPPER_W_1W(Grequest_complete)
DEFAULT_WRAPPER_W_5W(Grequest_start)
DEFAULT_WRAPPER_W_1W(Group_c2f)
DEFAULT_WRAPPER_W_3W(Group_compare)
DEFAULT_WRAPPER_W_3W(Group_difference)
DEFAULT_WRAPPER_W_4W(Group_excl)
DEFAULT_WRAPPER_W_1W(Group_f2c)
DEFAULT_WRAPPER_W_1W(Group_free)
DEFAULT_WRAPPER_W_4W(Group_incl)
DEFAULT_WRAPPER_W_3W(Group_intersection)
DEFAULT_WRAPPER_W_4W(Group_range_excl)
DEFAULT_WRAPPER_W_4W(Group_range_incl)
DEFAULT_WRAPPER_W_2W(Group_rank)
DEFAULT_WRAPPER_W_2W(Group_size)
DEFAULT_WRAPPER_W_5W(Group_translate_ranks)
DEFAULT_WRAPPER_W_3W(Group_union)
DEFAULT_WRAPPER_W_7W(Ibsend)
DEFAULT_WRAPPER_W_1W(Info_c2f)
DEFAULT_WRAPPER_W_1W(Info_create)
DEFAULT_WRAPPER_W_2W(Info_delete)
DEFAULT_WRAPPER_W_2W(Info_dup)
DEFAULT_WRAPPER_W_1W(Info_f2c)
DEFAULT_WRAPPER_W_1W(Info_free)
DEFAULT_WRAPPER_W_5W(Info_get)
DEFAULT_WRAPPER_W_2W(Info_get_nkeys)
DEFAULT_WRAPPER_W_3W(Info_get_nthkey)
DEFAULT_WRAPPER_W_4W(Info_get_valuelen)
DEFAULT_WRAPPER_W_3W(Info_set)
/* DEFAULT_WRAPPER_W_2W(Init) */
/* DEFAULT_WRAPPER_W_1W(Initialized) */
DEFAULT_WRAPPER_W_4W(Init_thread)
DEFAULT_WRAPPER_W_6W(Intercomm_create)
DEFAULT_WRAPPER_W_3W(Intercomm_merge)
/* DEFAULT_WRAPPER_W_5W(Iprobe) */
/* DEFAULT_WRAPPER_W_7W(Irecv) */
DEFAULT_WRAPPER_W_7W(Irsend)
/* DEFAULT_WRAPPER_W_7W(Isend) */
DEFAULT_WRAPPER_W_7W(Issend)
DEFAULT_WRAPPER_W_1W(Is_thread_main)
DEFAULT_WRAPPER_W_4W(Keyval_create)
DEFAULT_WRAPPER_W_1W(Keyval_free)
DEFAULT_WRAPPER_W_3W(Lookup_name)
DEFAULT_WRAPPER_W_1W(Op_c2f)
/* DEFAULT_WRAPPER_W_3W(Op_create) */
DEFAULT_WRAPPER_W_2W(Open_port)
DEFAULT_WRAPPER_W_1W(Op_f2c)
DEFAULT_WRAPPER_W_1W(Op_free)
DEFAULT_WRAPPER_W_7W(Pack_external)
DEFAULT_WRAPPER_W_4W(Pack_external_size)
DEFAULT_WRAPPER_W_7W(Pack)
DEFAULT_WRAPPER_W_4W(Pack_size)
/* int MPI_Pcontrol(const int level, ...) */
DEFAULT_WRAPPER_W_4W(Probe)
DEFAULT_WRAPPER_W_3W(Publish_name)
DEFAULT_WRAPPER_W_8W(Put)
DEFAULT_WRAPPER_W_1W(Query_thread)
DEFAULT_WRAPPER_W_7W(Recv_init)
/* DEFAULT_WRAPPER_W_7W(Recv) */
/* DEFAULT_WRAPPER_W_7W(Reduce) */
DEFAULT_WRAPPER_W_6W(Reduce_scatter)
DEFAULT_WRAPPER_W_5W(Register_datarep)
DEFAULT_WRAPPER_W_1W(Request_c2f)
DEFAULT_WRAPPER_W_1W(Request_f2c)
DEFAULT_WRAPPER_W_1W(Request_free)
DEFAULT_WRAPPER_W_3W(Request_get_status)
DEFAULT_WRAPPER_W_6W(Rsend)
DEFAULT_WRAPPER_W_7W(Rsend_init)
DEFAULT_WRAPPER_W_6W(Scan)
DEFAULT_WRAPPER_W_8W(Scatter)
DEFAULT_WRAPPER_W_9W(Scatterv)
DEFAULT_WRAPPER_W_7W(Send_init)
/* DEFAULT_WRAPPER_W_6W(Send) */
/* DEFAULT_WRAPPER_W_12W(Sendrecv) */
DEFAULT_WRAPPER_W_9W(Sendrecv_replace)
DEFAULT_WRAPPER_W_7W(Ssend_init)
DEFAULT_WRAPPER_W_6W(Ssend)
DEFAULT_WRAPPER_W_1W(Start)
DEFAULT_WRAPPER_W_2W(Startall)
DEFAULT_WRAPPER_W_2W(Status_c2f)
DEFAULT_WRAPPER_W_2W(Status_f2c)
DEFAULT_WRAPPER_W_2W(Status_set_cancelled)
DEFAULT_WRAPPER_W_3W(Status_set_elements)
DEFAULT_WRAPPER_W_4W(Testall)
DEFAULT_WRAPPER_W_5W(Testany)
DEFAULT_WRAPPER_W_3W(Test)
DEFAULT_WRAPPER_W_2W(Test_cancelled)
DEFAULT_WRAPPER_W_5W(Testsome)
DEFAULT_WRAPPER_W_2W(Topo_test)
DEFAULT_WRAPPER_W_1W(Type_c2f)
DEFAULT_WRAPPER_W_1W(Type_commit)
DEFAULT_WRAPPER_W_3W(Type_contiguous)
DEFAULT_WRAPPER_W_10W(Type_create_darray)
DEFAULT_WRAPPER_W_3W(Type_create_f90_complex)
DEFAULT_WRAPPER_W_2W(Type_create_f90_integer)
DEFAULT_WRAPPER_W_3W(Type_create_f90_real)
DEFAULT_WRAPPER_W_5W(Type_create_hindexed)
DEFAULT_WRAPPER_W_5W(Type_create_hvector)
DEFAULT_WRAPPER_W_4W(Type_create_keyval)
DEFAULT_WRAPPER_W_5W(Type_create_indexed_block)
DEFAULT_WRAPPER_W_5W(Type_create_struct)
DEFAULT_WRAPPER_W_7W(Type_create_subarray)
DEFAULT_WRAPPER_W_4W(Type_create_resized)
DEFAULT_WRAPPER_W_2W(Type_delete_attr)
DEFAULT_WRAPPER_W_2W(Type_dup)
DEFAULT_WRAPPER_W_2W(Type_extent)
DEFAULT_WRAPPER_W_1W(Type_free)
DEFAULT_WRAPPER_W_1W(Type_free_keyval)
DEFAULT_WRAPPER_W_1W(Type_f2c)
DEFAULT_WRAPPER_W_4W(Type_get_attr)
DEFAULT_WRAPPER_W_7W(Type_get_contents)
DEFAULT_WRAPPER_W_5W(Type_get_envelope)
DEFAULT_WRAPPER_W_3W(Type_get_extent)
DEFAULT_WRAPPER_W_3W(Type_get_name)
DEFAULT_WRAPPER_W_3W(Type_get_true_extent)
DEFAULT_WRAPPER_W_5W(Type_hindexed)
DEFAULT_WRAPPER_W_5W(Type_hvector)
DEFAULT_WRAPPER_W_5W(Type_indexed)
DEFAULT_WRAPPER_W_2W(Type_lb)
DEFAULT_WRAPPER_W_3W(Type_match_size)
DEFAULT_WRAPPER_W_3W(Type_set_attr)
DEFAULT_WRAPPER_W_2W(Type_set_name)
DEFAULT_WRAPPER_W_2W(Type_size)
DEFAULT_WRAPPER_W_5W(Type_struct)
DEFAULT_WRAPPER_W_2W(Type_ub)
DEFAULT_WRAPPER_W_5W(Type_vector)
DEFAULT_WRAPPER_W_7W(Unpack)
DEFAULT_WRAPPER_W_3W(Unpublish_name)
DEFAULT_WRAPPER_W_7W(Unpack_external)
/* DEFAULT_WRAPPER_W_3W(Waitall) */
DEFAULT_WRAPPER_W_4W(Waitany)
/* DEFAULT_WRAPPER_W_2W(Wait) */
DEFAULT_WRAPPER_W_5W(Waitsome)
DEFAULT_WRAPPER_W_1W(Win_c2f)
DEFAULT_WRAPPER_W_2W(Win_call_errhandler)
DEFAULT_WRAPPER_W_1W(Win_complete)
DEFAULT_WRAPPER_W_6W(Win_create)
DEFAULT_WRAPPER_W_2W(Win_create_errhandler)
DEFAULT_WRAPPER_W_4W(Win_create_keyval)
DEFAULT_WRAPPER_W_2W(Win_delete_attr)
DEFAULT_WRAPPER_W_1W(Win_f2c)
DEFAULT_WRAPPER_W_2W(Win_fence)
DEFAULT_WRAPPER_W_1W(Win_free)
DEFAULT_WRAPPER_W_1W(Win_free_keyval)
DEFAULT_WRAPPER_W_4W(Win_get_attr)
DEFAULT_WRAPPER_W_2W(Win_get_errhandler)
DEFAULT_WRAPPER_W_2W(Win_get_group)
DEFAULT_WRAPPER_W_3W(Win_get_name)
DEFAULT_WRAPPER_W_4W(Win_lock)
DEFAULT_WRAPPER_W_3W(Win_post)
DEFAULT_WRAPPER_W_3W(Win_set_attr)
DEFAULT_WRAPPER_W_2W(Win_set_errhandler)
DEFAULT_WRAPPER_W_2W(Win_set_name)
DEFAULT_WRAPPER_W_3W(Win_start)
DEFAULT_WRAPPER_W_2W(Win_test)
DEFAULT_WRAPPER_W_2W(Win_unlock)
DEFAULT_WRAPPER_W_1W(Win_wait)
/* double MPI_Wtick(void) */
/* double MPI_Wtime(void) */


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*---                                                      ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/*---------------------------------------------------------------*/
/*--- end                                           mpiwrap.c ---*/
/*---------------------------------------------------------------*/

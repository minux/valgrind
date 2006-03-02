
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
   OpenPMPI lib/libmpi.so,  soname = libmpi.so.0
*/
/* ifdef OpenMPI ... */
#define I_WRAP_FNNAME_U(_name) I_WRAP_SONAME_FNNAME_ZU(libmpiZdsoZa,_name)


/*------------------------------------------------------------*/
/*--- Decls                                                ---*/
/*------------------------------------------------------------*/

typedef  unsigned char  Bool;
#define False ((Bool)0)
#define True  ((Bool)1)


/*------------------------------------------------------------*/
/*--- Simple helpers                                       ---*/
/*------------------------------------------------------------*/

/* ------ Helpers for debug printing ------ */

/* constant */
static const char* preamble = "valgrind MPI wrappers";

/* established at startup */
static pid_t my_pid      = -1;
static char* options_str = NULL;
static Bool  opt_verbose = False;
static Bool  opt_strict  = False;
static Bool  opt_help    = False;

static inline void before ( char* fnname )
{
   /* This isn't thread-safe wrt 'done' (no locking).  It's not
      critical. */
   static int done = 0;
   if (done == 0) {
      done = 1;
      my_pid = getpid();
      options_str = getenv("MPIWRAP_DEBUG");
      if (options_str) 
         opt_help    = NULL != strstr(options_str, "help");
      if (options_str) 
         opt_verbose = NULL != strstr(options_str, "verbose");
      if (options_str) 
         opt_strict  = NULL != strstr(options_str, "strict");
      fprintf(stderr, "%s %5d: Active for pid %d\n", 
                      preamble, my_pid, my_pid);
      if (opt_help) {
         fprintf(stderr, "\n");
         fprintf(stderr, "Valid options for the MPIWRAP_DEBUG environment"
                         " variable are:\n");
         fprintf(stderr, "\n");
         fprintf(stderr, "   verbose    show wrapper entries/exits\n");
         fprintf(stderr, "   strict     abort the program if a function"
                         " with no wrapper is used\n");
         fprintf(stderr, "   help       display this message, then exit\n");
         fprintf(stderr, "\n");
         fprintf(stderr, "Multiple options are allowed, eg"
                         " MPIWRAP_DEBUG=strict,verbose\n");
         fprintf(stderr, "\n");
         fprintf(stderr, "%s %5d: exiting now\n", preamble, my_pid );
         exit(1);
      }
      fprintf(stderr, "%s %5d: Try MPIWRAP_DEBUG=help for possible options\n", 
                      preamble, my_pid);
      if (opt_verbose || opt_strict)
         fprintf(stderr, "%s %5d: Selected options: %s %s\n", 
                         preamble, my_pid, opt_verbose ? "verbose" : "",
                                           opt_strict  ? "strict"  : "");

   }
   if (opt_verbose)
      fprintf(stderr, "%s %5d: enter PMPI_%s\n", preamble,  my_pid, fnname );
}

static inline void after ( char* fnname, int err )
{
   if (opt_verbose)
      fprintf(stderr, "%s %5d:  exit PMPI_%s (err = %d)\n", 
                      preamble, my_pid, fnname, err );
}

static void barf ( char* msg )
{
   fprintf(stderr, "%s %5d: fatal: %s\n",   preamble, my_pid, msg);
   fprintf(stderr, "%s %5d: exiting now\n", preamble, my_pid );
   exit(1);
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


/*------------------------------------------------------------*/
/*--- Address-range helpers                                ---*/
/*------------------------------------------------------------*/

/* ----------------
   Do checks on memory areas defined using the MPI (buffer, count,
   type) convention.
   ----------------
*/

static inline long datasize ( MPI_Datatype datatype )
{
   if (datatype == MPI_INT)        return sizeof(signed int);
   if (datatype == MPI_DOUBLE)     return sizeof(double);
   if (datatype == MPI_CHAR)       return sizeof(signed char);
   if (datatype == MPI_UNSIGNED)   return sizeof(unsigned int);
   fprintf(stderr, "%s %5d: datasize: unhandled PMPI_Datatype %d\n",
                   preamble, my_pid, (int)datatype);
   return 0;
}


/* Check that the specified area is both addressible and contains
   initialised data, and cause V to complain if not. */

static
void check_readable ( void *buffer, long count, MPI_Datatype datatype )
{
   long nbytes = count * datasize(datatype);
   if (nbytes > 0) {
      VALGRIND_CHECK_READABLE(buffer, nbytes);
   }
}


/* Check that the specified area is addressible, and cause V to
   complain if not. Doesn't matter whether the data there is
   initialised or not. */

static
void check_writable ( void *buffer, long count, MPI_Datatype datatype )
{
   long nbytes = count * datasize(datatype);
   if (nbytes > 0) {
      VALGRIND_CHECK_WRITABLE(buffer, nbytes);
   }
}


/* Set the specified area to 'addressible and defined' (safe-to-read)
   state. */

static
void make_readable ( void *buffer, int count, MPI_Datatype datatype )
{
   long nbytes = count * datasize(datatype);
   if (nbytes > 0) {
      VALGRIND_MAKE_READABLE(buffer, nbytes);
   }
}

static
void 
make_readable_if_success ( int err, void *buffer, int count, 
                                    MPI_Datatype datatype )
{
   if (err == MPI_SUCCESS) {
      long nbytes = count * datasize(datatype);
      if (nbytes > 0) {
         VALGRIND_MAKE_READABLE(buffer, nbytes);
      }
   }
}


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
void make_readable_untyped ( void* buffer, long nbytes )
{
   if (nbytes > 0) {
      VALGRIND_MAKE_READABLE(buffer, nbytes);
   }
}

static inline
void make_readable_if_success_untyped ( int err, 
                                        void* buffer, long nbytes )
{
   if (err == MPI_SUCCESS && nbytes > 0) {
      VALGRIND_MAKE_READABLE(buffer, nbytes);
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

UNIMPLEMENTED_WRAPPER(Bsend)
UNIMPLEMENTED_WRAPPER(Ssend)
UNIMPLEMENTED_WRAPPER(Rsend)

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
      make_readable(buf, recv_count, datatype);
   }
   after("Recv", err);
   return err;
}

/* PMPI_Get_count is used by this library (no matter, we just need to
   supply a wrapper).  Since there's nothing much to wrap, supply a
   no-op wrapper. */
NO_OP_WRAPPER(Get_count)

UNIMPLEMENTED_WRAPPER(Buffer_attach)
UNIMPLEMENTED_WRAPPER(Buffer_detach)


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
//static pthread_mutex_t sReqs_lock = PTHREAD_MUTEX_INITIALIZER;


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
         /* UNLOCK */
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
   /* LOCK */
   for (i = 0; i < sReqs_used; i++) {
      if (sReqs[i].inUse && eq_MPI_Request(sReqs[i].key,request)) {
         ret = &sReqs[i];
         break;
      }
   }
   /* UNLOCK */
   return ret;
}


/* Delete shadow info for 'request', if any. */

static void delete_shadow_Request ( MPI_Request request )
{
   int i;
   /* LOCK */
   for (i = 0; i < sReqs_used; i++) {
      if (sReqs[i].inUse && eq_MPI_Request(sReqs[i].key,request)) {
         sReqs[i].inUse = False;
         break;
      }
   }
   /* UNLOCK */
}


/* Add a shadow for 'request', overwriting any old binding for it. */

static 
void add_shadow_Request( MPI_Request request, 
                         void* buf, int count, 
                         MPI_Datatype datatype )
{
   int i, ix = -1;
   /* LOCK */
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

   /* UNLOCK */
   if (opt_verbose)
      fprintf(stderr, "%s %5d: sReq+ 0x%lx -> b/c/d %p/%d/0x%lx [slot %d]\n",
                      preamble, my_pid, (unsigned long)request, 
                                buf, count, (long)datatype, ix);
}


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
         make_readable(shadow->buf, recv_count, shadow->datatype);
         if (opt_verbose)
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
   make_readable_if_success_untyped(err, request, sizeof(*request));
   after("Isend", err);
   return err;
}

UNIMPLEMENTED_WRAPPER(Ibsend)
UNIMPLEMENTED_WRAPPER(Issend)
UNIMPLEMENTED_WRAPPER(Irsend)

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
      make_readable_untyped(request, sizeof(*request));
      add_shadow_Request( *request, buf,count,datatype );
   }
   after("Irecv", err);
   return err;
}

UNIMPLEMENTED_WRAPPER(Request_free)

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
      make_readable_untyped(status, sizeof(MPI_Status));
   }
   after("Wait", err);
   return err;
}

UNIMPLEMENTED_WRAPPER(Test)

UNIMPLEMENTED_WRAPPER(Waitany)
UNIMPLEMENTED_WRAPPER(Testany)

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
         make_readable_untyped(&statuses[i], sizeof(MPI_Status));
      }
   }
   if (requests_before)
      free(requests_before);
   after("Waitall", err);
   return err;
}

UNIMPLEMENTED_WRAPPER(Testall)

UNIMPLEMENTED_WRAPPER(Waitsome)
UNIMPLEMENTED_WRAPPER(Testsome)

UNIMPLEMENTED_WRAPPER(Probe)

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
      make_readable_untyped(flag, sizeof(*flag));
      if (*flag)
         make_readable_untyped(status, sizeof(*status));
      else
         make_writable_untyped(status, sizeof(*status));
   }
   after("Iprobe", err);
   return err;
}

UNIMPLEMENTED_WRAPPER(Cancel)
UNIMPLEMENTED_WRAPPER(Test_cancelled)


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
      make_readable(recvbuf, recvcount_actual, recvtype);
   }
   after("Sendrecv", err);
   return err;
}

UNIMPLEMENTED_WRAPPER(Sendrecv_replace)


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 3.12, Derived datatypes                          ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/* --- Address --- */
/* Does this have anything worth checking? */
NO_OP_WRAPPER(PMPI_Address)


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
   make_readable_if_success(err, buffer, count, datatype);
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
   make_readable_if_success(err, recvbuf, recvcount * sz, recvtype);
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
      make_readable_if_success(err, recvbuf, count, datatype);
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
   make_readable_if_success(err, recvbuf, count, datatype);
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
   make_readable_if_success_untyped(err, op, sizeof(*op));
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
   make_readable_if_success_untyped(err, rank, sizeof(*rank));
   after("Comm_rank", err);
   return err;
}

UNIMPLEMENTED_WRAPPER(Comm_remote_group)
UNIMPLEMENTED_WRAPPER(Comm_remote_size)
UNIMPLEMENTED_WRAPPER(Comm_set_errhandler)

/* The MPI 1.1 doc doesn't appear to mention this. */
NO_OP_WRAPPER(Comm_set_name)

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
   make_readable_if_success_untyped(err, size, sizeof(*size));
   after("Comm_size", err);
   return err;
}


/*------------------------------------------------------------*/
/*---                                                      ---*/
/*--- Sec 5.7, Caching                                     ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/* This messes with a couple of function pointers, an int* and an
   "extra state" area of indeterminate size.  I don't think there's
   much we can check here.  Hence: */
NO_OP_WRAPPER(Keyval_create)

/* Similar comment to Keyval_create */
NO_OP_WRAPPER(Keyval_free)


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
   make_readable_if_success_untyped(err, flag, sizeof(int));
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
/*---                                                      ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/

/*------------------------------------------------------------*/
/*---                                                      ---*/
/*---                                                      ---*/
/*---                                                      ---*/
/*------------------------------------------------------------*/



/* Here are the wrappers themselves. */
UNIMPLEMENTED_WRAPPER(Abort)



UNIMPLEMENTED_WRAPPER(Allgather)
UNIMPLEMENTED_WRAPPER(Allgatherv)


UNIMPLEMENTED_WRAPPER(Alltoall)
UNIMPLEMENTED_WRAPPER(Alltoallv)
UNIMPLEMENTED_WRAPPER(Alltoallw)
UNIMPLEMENTED_WRAPPER(Attr_delete)
NO_OP_WRAPPER(Attr_get)
NO_OP_WRAPPER(Attr_put)
NO_OP_WRAPPER(Barrier)


UNIMPLEMENTED_WRAPPER(Bsend_init)
UNIMPLEMENTED_WRAPPER(Cart_coords)
UNIMPLEMENTED_WRAPPER(Cart_create)
UNIMPLEMENTED_WRAPPER(Cart_get)
UNIMPLEMENTED_WRAPPER(Cart_map)
UNIMPLEMENTED_WRAPPER(Cart_rank)
UNIMPLEMENTED_WRAPPER(Cart_shift)
UNIMPLEMENTED_WRAPPER(Cart_sub)
UNIMPLEMENTED_WRAPPER(Cartdim_get)
UNIMPLEMENTED_WRAPPER(Comm_compare)
NO_OP_WRAPPER(Comm_create)
UNIMPLEMENTED_WRAPPER(Comm_create_errhandler)
UNIMPLEMENTED_WRAPPER(Comm_dup)
NO_OP_WRAPPER(Comm_free)
UNIMPLEMENTED_WRAPPER(Comm_free_errhandler)
UNIMPLEMENTED_WRAPPER(Comm_get_errhandler)
UNIMPLEMENTED_WRAPPER(Comm_get_name)
NO_OP_WRAPPER(Comm_group)


UNIMPLEMENTED_WRAPPER(Comm_split)
UNIMPLEMENTED_WRAPPER(Comm_test_inter)
UNIMPLEMENTED_WRAPPER(Dims_create)
UNIMPLEMENTED_WRAPPER(Errhandler_create)

//UNIMPLEMENTED_WRAPPER(Errhandler_free)
NO_OP_WRAPPER(Errhandler_free)

UNIMPLEMENTED_WRAPPER(Errhandler_get)

//UNIMPLEMENTED_WRAPPER(Errhandler_set)
NO_OP_WRAPPER(Errhandler_set)

UNIMPLEMENTED_WRAPPER(Error_class)
UNIMPLEMENTED_WRAPPER(Error_string)

UNIMPLEMENTED_WRAPPER(Finalized)


UNIMPLEMENTED_WRAPPER(Gatherv)
UNIMPLEMENTED_WRAPPER(Get_address)


UNIMPLEMENTED_WRAPPER(Get_elements)
NO_OP_WRAPPER(Get_processor_name)
UNIMPLEMENTED_WRAPPER(Get_version)
UNIMPLEMENTED_WRAPPER(Graph_create)
UNIMPLEMENTED_WRAPPER(Graph_get)
UNIMPLEMENTED_WRAPPER(Graph_map)
UNIMPLEMENTED_WRAPPER(Graph_neighbors)
UNIMPLEMENTED_WRAPPER(Graph_neighbors_count)
UNIMPLEMENTED_WRAPPER(Graphdims_get)
UNIMPLEMENTED_WRAPPER(Group_compare)
UNIMPLEMENTED_WRAPPER(Group_difference)
NO_OP_WRAPPER(Group_excl)
NO_OP_WRAPPER(Group_free)
NO_OP_WRAPPER(Group_incl)
UNIMPLEMENTED_WRAPPER(Group_intersection)
UNIMPLEMENTED_WRAPPER(Group_range_excl)
UNIMPLEMENTED_WRAPPER(Group_range_incl)
UNIMPLEMENTED_WRAPPER(Group_rank)
UNIMPLEMENTED_WRAPPER(Group_size)
UNIMPLEMENTED_WRAPPER(Group_translate_ranks)
UNIMPLEMENTED_WRAPPER(Group_union)


UNIMPLEMENTED_WRAPPER(Init_thread)


UNIMPLEMENTED_WRAPPER(Intercomm_create)
UNIMPLEMENTED_WRAPPER(Intercomm_merge)



NO_OP_WRAPPER(Op_free)
UNIMPLEMENTED_WRAPPER(Pack)
UNIMPLEMENTED_WRAPPER(Pack_size)
UNIMPLEMENTED_WRAPPER(Pcontrol)
UNIMPLEMENTED_WRAPPER(Query_thread)


UNIMPLEMENTED_WRAPPER(Recv_init)


UNIMPLEMENTED_WRAPPER(Reduce_scatter)
UNIMPLEMENTED_WRAPPER(Rsend_init)
UNIMPLEMENTED_WRAPPER(Scan)
UNIMPLEMENTED_WRAPPER(Scatter)
UNIMPLEMENTED_WRAPPER(Scatterv)


UNIMPLEMENTED_WRAPPER(Send_init)


UNIMPLEMENTED_WRAPPER(Ssend_init)
UNIMPLEMENTED_WRAPPER(Start)
UNIMPLEMENTED_WRAPPER(Startall)
UNIMPLEMENTED_WRAPPER(Status_set_cancelled)
UNIMPLEMENTED_WRAPPER(Status_set_elements)
UNIMPLEMENTED_WRAPPER(Topo_test)

//UNIMPLEMENTED_WRAPPER(Type_commit)
NO_OP_WRAPPER(Type_commit)

//UNIMPLEMENTED_WRAPPER(Type_contiguous)
NO_OP_WRAPPER(Type_contiguous)

UNIMPLEMENTED_WRAPPER(Type_count)
UNIMPLEMENTED_WRAPPER(Type_create_darray)
UNIMPLEMENTED_WRAPPER(Type_create_subarray)
UNIMPLEMENTED_WRAPPER(Type_extent)
UNIMPLEMENTED_WRAPPER(Type_free)
UNIMPLEMENTED_WRAPPER(Type_get_contents)
UNIMPLEMENTED_WRAPPER(Type_get_envelope)
UNIMPLEMENTED_WRAPPER(Type_get_name)
UNIMPLEMENTED_WRAPPER(Type_create_hindexed)
UNIMPLEMENTED_WRAPPER(Type_create_hvector)

//UNIMPLEMENTED_WRAPPER(Type_create_struct)
NO_OP_WRAPPER(Type_create_struct)

UNIMPLEMENTED_WRAPPER(Type_hindexed)
UNIMPLEMENTED_WRAPPER(Type_hvector)
UNIMPLEMENTED_WRAPPER(Type_indexed)
UNIMPLEMENTED_WRAPPER(Type_lb)
UNIMPLEMENTED_WRAPPER(Type_set_name)
UNIMPLEMENTED_WRAPPER(Type_size)

//UNIMPLEMENTED_WRAPPER(Type_struct)
NO_OP_WRAPPER(Type_struct)

UNIMPLEMENTED_WRAPPER(Type_ub)
UNIMPLEMENTED_WRAPPER(Type_vector)
UNIMPLEMENTED_WRAPPER(Unpack)


UNIMPLEMENTED_WRAPPER(Win_create_errhandler)
UNIMPLEMENTED_WRAPPER(Win_free_errhandler)
UNIMPLEMENTED_WRAPPER(Win_get_errhandler)
UNIMPLEMENTED_WRAPPER(Win_set_errhandler)
NO_OP_WRAPPER(Wtick)
NO_OP_WRAPPER(Wtime)


/*---------------------------------------------------------------*/
/*--- end                                           mpiwrap.c ---*/
/*---------------------------------------------------------------*/


/*
   ----------------------------------------------------------------

   Notice that the following BSD-style license applies to this one
   file (valgrind.h) only.  The entire rest of Valgrind is licensed
   under the terms of the GNU General Public License, version 2.  See
   the COPYING file in the source distribution for details.

   ----------------------------------------------------------------

   This file is part of Valgrind, an extensible x86 protected-mode
   emulator for monitoring program execution on x86-Unixes.

   Copyright (C) 2000-2004 Julian Seward.  All rights reserved.

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

   ----------------------------------------------------------------

   Notice that the above BSD-style license applies to this one file
   (valgrind.h) only.  The entire rest of Valgrind is licensed under
   the terms of the GNU General Public License, version 2.  See the
   COPYING file in the source distribution for details.

   ---------------------------------------------------------------- 
*/


#ifndef __VALGRIND_H
#define __VALGRIND_H

#include <stdarg.h>


/* This file is for inclusion into client (your!) code.

   You can use these macros to manipulate and query Valgrind's 
   execution inside your own programs.

   The resulting executables will still run without Valgrind, just a
   little bit more slowly than they otherwise would, but otherwise
   unchanged.  When not running on valgrind, each client request
   consumes about 9 x86 instructions, so the resulting performance
   loss is negligible unless you plan to execute client requests
   millions of times per second.  Nevertheless, if that is still a
   problem, you can compile with the NVALGRIND symbol defined (gcc
   -DNVALGRIND) so that client requests are not even compiled in.  */



#ifndef NVALGRIND
/* This defines the magic code sequence which the JITter spots and
   handles magically.  Don't look too closely at this; it will rot
   your brain.  Valgrind dumps the result value in %EDX, so we first
   copy the default value there, so that it is returned when not
   running on Valgrind.  Since %EAX points to a block of mem
   containing the args, you can pass as many args as you want like
   this.  Currently this is set up to deal with 4 args since that's
   the max that we appear to need (pthread_create).  
*/
#define VALGRIND_MAGIC_SEQUENCE(                                        \
        _zzq_rlval,   /* result lvalue */                               \
        _zzq_default, /* result returned when running on real CPU */    \
        _zzq_request, /* request code */                                \
        _zzq_arg1,    /* request first param */                         \
        _zzq_arg2,    /* request second param */                        \
        _zzq_arg3,    /* request third param */                         \
        _zzq_arg4     /* request fourth param */ )                      \
                                                                        \
  { volatile unsigned int _zzq_args[5];                                 \
    _zzq_args[0] = (volatile unsigned int)(_zzq_request);               \
    _zzq_args[1] = (volatile unsigned int)(_zzq_arg1);                  \
    _zzq_args[2] = (volatile unsigned int)(_zzq_arg2);                  \
    _zzq_args[3] = (volatile unsigned int)(_zzq_arg3);                  \
    _zzq_args[4] = (volatile unsigned int)(_zzq_arg4);                  \
    asm volatile("movl %1, %%eax\n\t"                                   \
                 "movl %2, %%edx\n\t"                                   \
                 "roll $29, %%eax ; roll $3, %%eax\n\t"                 \
                 "rorl $27, %%eax ; rorl $5, %%eax\n\t"                 \
                 "roll $13, %%eax ; roll $19, %%eax\n\t"                \
                 "movl %%edx, %0\t"                                     \
                 : "=r" (_zzq_rlval)                                    \
                 : "r" (&_zzq_args[0]), "r" (_zzq_default)              \
                 : "eax", "edx", "cc", "memory"                         \
                );                                                      \
  }
#else  /* NVALGRIND */
/* Define NVALGRIND to completely remove the Valgrind magic sequence
   from the compiled code (analogous to NDEBUG's effects on
   assert())  */
#define VALGRIND_MAGIC_SEQUENCE(					\
        _zzq_rlval,   /* result lvalue */				\
        _zzq_default, /* result returned when running on real CPU */	\
        _zzq_request, /* request code */				\
        _zzq_arg1,    /* request first param */				\
        _zzq_arg2,    /* request second param */			\
        _zzq_arg3,    /* request third param */				\
        _zzq_arg4     /* request fourth param */ )			\
   {									\
      (_zzq_rlval) = (_zzq_default);					\
   }
#endif /* NVALGRIND */

/* Some request codes.  There are many more of these, but most are not
   exposed to end-user view.  These are the public ones, all of the
   form 0x1000 + small_number.

   Core ones are in the range 0x00000000--0x0000ffff.  The non-public ones
   start at 0x2000.
*/

#define VG_USERREQ_SKIN_BASE(a,b) \
   ((unsigned int)(((a)&0xff) << 24 | ((b)&0xff) << 16))
#define VG_IS_SKIN_USERREQ(a, b, v) \
   (VG_USERREQ_SKIN_BASE(a,b) == ((v) & 0xffff0000))

typedef
   enum { VG_USERREQ__RUNNING_ON_VALGRIND  = 0x1001,
          VG_USERREQ__DISCARD_TRANSLATIONS = 0x1002,

          /* These allow any function of 0--3 args to be called from the
             simulated CPU but run on the real CPU */
          VG_USERREQ__CLIENT_CALL0 = 0x1101,
          VG_USERREQ__CLIENT_CALL1 = 0x1102,
          VG_USERREQ__CLIENT_CALL2 = 0x1103,
          VG_USERREQ__CLIENT_CALL3 = 0x1104,

          /* Can be useful in regression testing suites -- eg. can send
             Valgrind's output to /dev/null and still count errors. */
          VG_USERREQ__COUNT_ERRORS = 0x1201,

          /* These are useful and can be interpreted by any tool that tracks
             malloc() et al, by using vg_replace_malloc.c. */
          VG_USERREQ__MALLOCLIKE_BLOCK = 0x1301,
          VG_USERREQ__FREELIKE_BLOCK   = 0x1302,
          /* Memory pool support. */
          VG_USERREQ__CREATE_MEMPOOL   = 0x1303,
          VG_USERREQ__DESTROY_MEMPOOL  = 0x1304,
          VG_USERREQ__MEMPOOL_ALLOC    = 0x1305,
          VG_USERREQ__MEMPOOL_FREE     = 0x1306,

          /* Allow printfs to valgrind log. */
          VG_USERREQ__PRINTF = 0x1401,
          VG_USERREQ__PRINTF_BACKTRACE = 0x1402
   } Vg_ClientRequest;

#ifndef __GNUC__
#define __extension__
#endif

/* Returns 1 if running on Valgrind, 0 if running on the real CPU. 
   Currently implemented but untested. */
#define RUNNING_ON_VALGRIND  __extension__                         \
   ({unsigned int _qzz_res;                                        \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0 /* returned if not */,     \
                            VG_USERREQ__RUNNING_ON_VALGRIND,       \
                            0, 0, 0, 0);                           \
    _qzz_res;                                                      \
   })


/* Discard translation of code in the range [_qzz_addr .. _qzz_addr +
   _qzz_len - 1].  Useful if you are debugging a JITter or some such,
   since it provides a way to make sure valgrind will retranslate the
   invalidated area.  Returns no value. */
#define VALGRIND_DISCARD_TRANSLATIONS(_qzz_addr,_qzz_len)          \
   {unsigned int _qzz_res;                                         \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0,                           \
                            VG_USERREQ__DISCARD_TRANSLATIONS,      \
                            _qzz_addr, _qzz_len, 0, 0);            \
   }

#ifndef NVALGRIND

int VALGRIND_PRINTF(const char *format, ...)
   __attribute__((format(__printf__, 1, 2)));
__attribute__((weak))
int
VALGRIND_PRINTF(const char *format, ...)
{
   unsigned int _qzz_res;
   va_list vargs;
   va_start(vargs, format);
   VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0, VG_USERREQ__PRINTF,
                           (unsigned int)format, (unsigned int)vargs, 0, 0);
   va_end(vargs);
   return _qzz_res;
}

int VALGRIND_PRINTF_BACKTRACE(const char *format, ...)
   __attribute__((format(__printf__, 1, 2)));
__attribute__((weak))
int
VALGRIND_PRINTF_BACKTRACE(const char *format, ...)
{
   unsigned int _qzz_res;
   va_list vargs;
   va_start(vargs, format);
   VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0, VG_USERREQ__PRINTF_BACKTRACE,
                           (unsigned int)format, (unsigned int)vargs, 0, 0);
   va_end(vargs);
   return _qzz_res;
}

#else /* NVALGRIND */

#define VALGRIND_PRINTF(...)
#define VALGRIND_PRINTF_BACKTRACE(...)

#endif /* NVALGRIND */

/* These requests allow control to move from the simulated CPU to the
   real CPU, calling an arbitary function */
#define VALGRIND_NON_SIMD_CALL0(_qyy_fn)                       \
   ({unsigned int _qyy_res;                                    \
    VALGRIND_MAGIC_SEQUENCE(_qyy_res, 0 /* default return */,  \
                            VG_USERREQ__CLIENT_CALL0,          \
                            _qyy_fn,                           \
                            0, 0, 0);                          \
    _qyy_res;                                                  \
   })

#define VALGRIND_NON_SIMD_CALL1(_qyy_fn, _qyy_arg1)            \
   ({unsigned int _qyy_res;                                    \
    VALGRIND_MAGIC_SEQUENCE(_qyy_res, 0 /* default return */,  \
                            VG_USERREQ__CLIENT_CALL1,          \
                            _qyy_fn,                           \
                            _qyy_arg1, 0, 0);                  \
    _qyy_res;                                                  \
   })

#define VALGRIND_NON_SIMD_CALL2(_qyy_fn, _qyy_arg1, _qyy_arg2) \
   ({unsigned int _qyy_res;                                    \
    VALGRIND_MAGIC_SEQUENCE(_qyy_res, 0 /* default return */,  \
                            VG_USERREQ__CLIENT_CALL2,          \
                            _qyy_fn,                           \
                            _qyy_arg1, _qyy_arg2, 0);          \
    _qyy_res;                                                  \
   })

#define VALGRIND_NON_SIMD_CALL3(_qyy_fn, _qyy_arg1, _qyy_arg2, _qyy_arg3)  \
   ({unsigned int _qyy_res;                                          \
    VALGRIND_MAGIC_SEQUENCE(_qyy_res, 0 /* default return */,        \
                            VG_USERREQ__CLIENT_CALL3,                \
                            _qyy_fn,                                 \
                            _qyy_arg1, _qyy_arg2, _qyy_arg3);        \
    _qyy_res;                                                        \
   })


/* Counts the number of errors that have been recorded by a tool.  Nb:
   the tool must record the errors with VG_(maybe_record_error)() or
   VG_(unique_error)() for them to be counted. */
#define VALGRIND_COUNT_ERRORS                                           \
   ({unsigned int _qyy_res;                                             \
    VALGRIND_MAGIC_SEQUENCE(_qyy_res, 0 /* default return */,           \
                            VG_USERREQ__COUNT_ERRORS,                   \
                            0, 0, 0, 0);                                \
    _qyy_res;                                                           \
   })

/* Mark a block of memory as having been allocated by a malloc()-like
   function.  `addr' is the start of the usable block (ie. after any
   redzone) `rzB' is redzone size if the allocator can apply redzones;
   use '0' if not.  Adding redzones makes it more likely Valgrind will spot
   block overruns.  `is_zeroed' indicates if the memory is zeroed, as it is
   for calloc().  Put it immediately after the point where a block is
   allocated. 
   
   If you're allocating memory via superblocks, and then handing out small
   chunks of each superblock, if you don't have redzones on your small
   blocks, it's worth marking the superblock with VALGRIND_MAKE_NOACCESS
   when it's created, so that block overruns are detected.  But if you can
   put redzones on, it's probably better to not do this, so that messages
   for small overruns are described in terms of the small block rather than
   the superblock (but if you have a big overrun that skips over a redzone,
   you could miss an error this way).  See memcheck/tests/custom_alloc.c
   for an example.

   Nb: block must be freed via a free()-like function specified
   with VALGRIND_FREELIKE_BLOCK or mismatch errors will occur. */
#define VALGRIND_MALLOCLIKE_BLOCK(addr, sizeB, rzB, is_zeroed)     \
   {unsigned int _qzz_res;                                         \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0,                           \
                            VG_USERREQ__MALLOCLIKE_BLOCK,          \
                            addr, sizeB, rzB, is_zeroed);          \
   }

/* Mark a block of memory as having been freed by a free()-like function.
   `rzB' is redzone size;  it must match that given to
   VALGRIND_MALLOCLIKE_BLOCK.  Memory not freed will be detected by the leak
   checker.  Put it immediately after the point where the block is freed. */
#define VALGRIND_FREELIKE_BLOCK(addr, rzB)                         \
   {unsigned int _qzz_res;                                         \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0,                           \
                            VG_USERREQ__FREELIKE_BLOCK,            \
                            addr, rzB, 0, 0);                      \
   }

/* Create a memory pool. */
#define VALGRIND_CREATE_MEMPOOL(pool, rzB, is_zeroed)              \
   {unsigned int _qzz_res;                                         \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0,                           \
                            VG_USERREQ__CREATE_MEMPOOL,            \
                            pool, rzB, is_zeroed, 0);              \
   }

/* Destroy a memory pool. */
#define VALGRIND_DESTROY_MEMPOOL(pool)                             \
   {unsigned int _qzz_res;                                         \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0,                           \
                            VG_USERREQ__DESTROY_MEMPOOL,           \
                            pool, 0, 0, 0);                        \
   }

/* Associate a piece of memory with a memory pool. */
#define VALGRIND_MEMPOOL_ALLOC(pool, addr, size)                   \
   {unsigned int _qzz_res;                                         \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0,                           \
                            VG_USERREQ__MEMPOOL_ALLOC,             \
                            pool, addr, size, 0);                  \
   }

/* Disassociate a piece of memory from a memory pool. */
#define VALGRIND_MEMPOOL_FREE(pool, addr)                          \
   {unsigned int _qzz_res;                                         \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0,                           \
                            VG_USERREQ__MEMPOOL_FREE,              \
                            pool, addr, 0, 0);                     \
   }

#endif   /* __VALGRIND_H */


/*
   ----------------------------------------------------------------

   Notice that the following BSD-style license applies to this one
   file (valgrind.h) only.  The entire rest of Valgrind is licensed
   under the terms of the GNU General Public License, version 2.  See
   the COPYING file in the source distribution for details.

   ----------------------------------------------------------------

   This file is part of Valgrind, an extensible x86 protected-mode
   emulator for monitoring program execution on x86-Unixes.

   Copyright (C) 2000-2002 Julian Seward.  All rights reserved.

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


#ifndef __VALGRIND_SOMESKIN_H
  #warning  For valgrind versions 1.9.0 and after, 
  #warning  you should not include valgrind.h directly.
  #warning  Instead include the .h relevant to the skin 
  #warning  you want to use.  For most people this means 
  #warning  you need to include memcheck.h instead of
  #warning  valgrind.h.
  #error    Compilation of your source will now abort.
#endif


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
*/

#define VG_USERREQ_SKIN_BASE(a,b)	((unsigned int)(((a)&0xff) << 24 | ((b)&0xff) << 16))
#define VG_IS_SKIN_USERREQ(a, b, v)	(VG_USERREQ_SKIN_BASE(a,b) == ((v) & 0xffff0000))

typedef
   enum { VG_USERREQ__RUNNING_ON_VALGRIND = 0x1001,
          VG_USERREQ__DISCARD_TRANSLATIONS,
          VG_USERREQ__FINAL_DUMMY_CLIENT_REQUEST
   } Vg_ClientRequest;


/* Returns 1 if running on Valgrind, 0 if running on the real CPU. 
   Currently implemented but untested. */
#define RUNNING_ON_VALGRIND                                        \
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


#endif

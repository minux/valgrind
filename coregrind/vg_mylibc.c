
/*--------------------------------------------------------------------*/
/*--- Reimplementation of some C library stuff, to avoid depending ---*/
/*--- on libc.so.                                                  ---*/
/*---                                                  vg_mylibc.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, an extensible x86 protected-mode
   emulator for monitoring program execution on x86-Unixes.

   Copyright (C) 2000-2003 Julian Seward 
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

#include "vg_include.h"



/* ---------------------------------------------------------------------
   Really Actually DO system calls.
   ------------------------------------------------------------------ */

/* Ripped off from /usr/include/asm/unistd.h. */

static
UInt vg_do_syscall0 ( UInt syscallno )
{ 
   UInt __res;
   __asm__ volatile ("int $0x80"
                     : "=a" (__res)
                     : "0" (syscallno) );
   return __res;
}


static
UInt vg_do_syscall1 ( UInt syscallno, UInt arg1 )
{ 
   UInt __res;
   __asm__ volatile ("int $0x80"
                     : "=a" (__res)
                     : "0" (syscallno),
                       "b" (arg1) );
   return __res;
}


static
UInt vg_do_syscall2 ( UInt syscallno, 
                      UInt arg1, UInt arg2 )
{ 
   UInt __res;
   __asm__ volatile ("int $0x80"
                     : "=a" (__res)
                     : "0" (syscallno),
                       "b" (arg1),
                       "c" (arg2) );
   return __res;
}


static
UInt vg_do_syscall3 ( UInt syscallno, 
                      UInt arg1, UInt arg2, UInt arg3 )
{ 
   UInt __res;
   __asm__ volatile ("int $0x80"
                     : "=a" (__res)
                     : "0" (syscallno),
                       "b" (arg1),
                       "c" (arg2),
                       "d" (arg3) );
   return __res;
}


static
UInt vg_do_syscall4 ( UInt syscallno, 
                      UInt arg1, UInt arg2, UInt arg3, UInt arg4 )
{ 
   UInt __res;
   __asm__ volatile ("int $0x80"
                     : "=a" (__res)
                     : "0" (syscallno),
                       "b" (arg1),
                       "c" (arg2),
                       "d" (arg3),
                       "S" (arg4) );
   return __res;
}


#if 0
static
UInt vg_do_syscall5 ( UInt syscallno, 
                      UInt arg1, UInt arg2, UInt arg3, UInt arg4, 
                      UInt arg5 )
{ 
   UInt __res;
   __asm__ volatile ("int $0x80"
                     : "=a" (__res)
                     : "0" (syscallno),
                       "b" (arg1),
                       "c" (arg2),
                       "d" (arg3),
                       "S" (arg4),
                       "D" (arg5) );
   return __res;
}
#endif

/* ---------------------------------------------------------------------
   Wrappers around system calls, and other stuff, to do with signals.
   ------------------------------------------------------------------ */

/* sigemptyset, sigfullset, sigaddset and sigdelset return 0 on
   success and -1 on error.  
*/
Int VG_(ksigfillset)( vki_ksigset_t* set )
{
   Int i;
   if (set == NULL)
      return -1;
   for (i = 0; i < VKI_KNSIG_WORDS; i++)
      set->ws[i] = 0xFFFFFFFF;
   return 0;
}

Int VG_(ksigemptyset)( vki_ksigset_t* set )
{
   Int i;
   if (set == NULL)
      return -1;
   for (i = 0; i < VKI_KNSIG_WORDS; i++)
      set->ws[i] = 0x0;
   return 0;
}

Bool VG_(kisemptysigset)( vki_ksigset_t* set )
{
   Int i;
   vg_assert(set != NULL);
   for (i = 0; i < VKI_KNSIG_WORDS; i++)
      if (set->ws[i] != 0x0) return False;
   return True;
}

Bool VG_(kisfullsigset)( vki_ksigset_t* set )
{
   Int i;
   vg_assert(set != NULL);
   for (i = 0; i < VKI_KNSIG_WORDS; i++)
      if (set->ws[i] != (UInt)(~0x0)) return False;
   return True;
}


Int VG_(ksigaddset)( vki_ksigset_t* set, Int signum )
{
   if (set == NULL)
      return -1;
   if (signum < 1 || signum > VKI_KNSIG)
      return -1;
   signum--;
   set->ws[signum / VKI_KNSIG_BPW] |= (1 << (signum % VKI_KNSIG_BPW));
   return 0;
}

Int VG_(ksigdelset)( vki_ksigset_t* set, Int signum )
{
   if (set == NULL)
      return -1;
   if (signum < 1 || signum > VKI_KNSIG)
      return -1;
   signum--;
   set->ws[signum / VKI_KNSIG_BPW] &= ~(1 << (signum % VKI_KNSIG_BPW));
   return 0;
}

Int VG_(ksigismember) ( vki_ksigset_t* set, Int signum )
{
   if (set == NULL)
      return 0;
   if (signum < 1 || signum > VKI_KNSIG)
      return 0;
   signum--;
   if (1 & ((set->ws[signum / VKI_KNSIG_BPW]) >> (signum % VKI_KNSIG_BPW)))
      return 1;
   else
      return 0;
}


/* Add all signals in src to dst. */
void VG_(ksigaddset_from_set)( vki_ksigset_t* dst, vki_ksigset_t* src )
{
   Int i;
   vg_assert(dst != NULL && src != NULL);
   for (i = 0; i < VKI_KNSIG_WORDS; i++)
      dst->ws[i] |= src->ws[i];
}

/* Remove all signals in src from dst. */
void VG_(ksigdelset_from_set)( vki_ksigset_t* dst, vki_ksigset_t* src )
{
   Int i;
   vg_assert(dst != NULL && src != NULL);
   for (i = 0; i < VKI_KNSIG_WORDS; i++)
      dst->ws[i] &= ~(src->ws[i]);
}


/* The functions sigaction, sigprocmask, sigpending and sigsuspend
   return 0 on success and -1 on error.  
*/
Int VG_(ksigprocmask)( Int how, 
                       const vki_ksigset_t* set, 
                       vki_ksigset_t* oldset)
{
   Int res 
      = vg_do_syscall4(__NR_rt_sigprocmask, 
                       how, (UInt)set, (UInt)oldset, 
                       VKI_KNSIG_WORDS * VKI_BYTES_PER_WORD);
   return VG_(is_kerror)(res) ? -1 : 0;
}


Int VG_(ksigaction) ( Int signum,  
                      const vki_ksigaction* act,  
                      vki_ksigaction* oldact)
{
   Int res
     = vg_do_syscall4(__NR_rt_sigaction,
                      signum, (UInt)act, (UInt)oldact, 
                      VKI_KNSIG_WORDS * VKI_BYTES_PER_WORD);
   /* VG_(printf)("res = %d\n",res); */
   return VG_(is_kerror)(res) ? -1 : 0;
}


Int VG_(ksigaltstack)( const vki_kstack_t* ss, vki_kstack_t* oss )
{
   Int res
     = vg_do_syscall2(__NR_sigaltstack, (UInt)ss, (UInt)oss);
   return VG_(is_kerror)(res) ? -1 : 0;
}

 
Int VG_(ksignal)(Int signum, void (*sighandler)(Int))
{
   Int res;
   vki_ksigaction sa;
   sa.ksa_handler = sighandler;
   sa.ksa_flags = VKI_SA_ONSTACK | VKI_SA_RESTART;
   sa.ksa_restorer = NULL;
   res = VG_(ksigemptyset)( &sa.ksa_mask );
   vg_assert(res == 0);
   res = vg_do_syscall4(__NR_rt_sigaction,
                        signum, (UInt)(&sa), (UInt)NULL,
                        VKI_KNSIG_WORDS * VKI_BYTES_PER_WORD);
   return VG_(is_kerror)(res) ? -1 : 0;
}


Int VG_(kkill)( Int pid, Int signo )
{
   Int res = vg_do_syscall2(__NR_kill, pid, signo);
   return VG_(is_kerror)(res) ? -1 : 0;
}


Int VG_(ksigpending) ( vki_ksigset_t* set )
{
   Int res = vg_do_syscall1(__NR_sigpending, (UInt)set);
   return VG_(is_kerror)(res) ? -1 : 0;
}


/* ---------------------------------------------------------------------
   mmap/munmap, exit, fcntl
   ------------------------------------------------------------------ */

/* Returns -1 on failure. */
void* VG_(mmap)( void* start, UInt length, 
                 UInt prot, UInt flags, UInt fd, UInt offset)
{
   Int  res;
   UInt args[6];
   args[0] = (UInt)start;
   args[1] = length;
   args[2] = prot;
   args[3] = flags;
   args[4] = fd;
   args[5] = offset;
   res = vg_do_syscall1(__NR_mmap, (UInt)(&(args[0])) );
   return VG_(is_kerror)(res) ? ((void*)(-1)) : (void*)res;
}

/* Returns -1 on failure. */
Int VG_(munmap)( void* start, Int length )
{
   Int res = vg_do_syscall2(__NR_munmap, (UInt)start, (UInt)length );
   return VG_(is_kerror)(res) ? -1 : 0;
}

void VG_(exit)( Int status )
{
   (void)vg_do_syscall1(__NR_exit, (UInt)status );
   /* Why are we still alive here? */
   /*NOTREACHED*/
   vg_assert(2+2 == 5);
}

/* Returns -1 on error. */
Int VG_(fcntl) ( Int fd, Int cmd, Int arg )
{
   Int res = vg_do_syscall3(__NR_fcntl, fd, cmd, arg);
   return VG_(is_kerror)(res) ? -1 : res;
}

/* Returns -1 on error. */
Int VG_(select)( Int n, 
                 vki_fd_set* readfds, 
                 vki_fd_set* writefds, 
                 vki_fd_set* exceptfds, 
                 struct vki_timeval * timeout )
{
   Int res;
   UInt args[5];
   args[0] = n;
   args[1] = (UInt)readfds;
   args[2] = (UInt)writefds;
   args[3] = (UInt)exceptfds;
   args[4] = (UInt)timeout;
   res = vg_do_syscall1(__NR_select, (UInt)(&(args[0])) );
   return VG_(is_kerror)(res) ? -1 : res;
}

/* Returns -1 on error, 0 if ok, 1 if interrupted. */
Int VG_(nanosleep)( const struct vki_timespec *req, 
                    struct vki_timespec *rem )
{
   Int res;
   res = vg_do_syscall2(__NR_nanosleep, (UInt)req, (UInt)rem);
   if (res == -VKI_EINVAL) return -1;
   if (res == -VKI_EINTR)  return 1;
   return 0;
}

void* VG_(brk) ( void* end_data_segment )
{
   Int res;
   res = vg_do_syscall1(__NR_brk, (UInt)end_data_segment);
   return (void*)(  VG_(is_kerror)(res) ? -1 : res  );
}


/* ---------------------------------------------------------------------
   printf implementation.  The key function, vg_vprintf(), emits chars 
   into a caller-supplied function.  Distantly derived from:

      vprintf replacement for Checker.
      Copyright 1993, 1994, 1995 Tristan Gingold
      Written September 1993 Tristan Gingold
      Tristan Gingold, 8 rue Parmentier, F-91120 PALAISEAU, FRANCE

   (Checker itself was GPL'd.)
   ------------------------------------------------------------------ */


/* Some flags.  */
#define VG_MSG_SIGNED    1 /* The value is signed. */
#define VG_MSG_ZJUSTIFY  2 /* Must justify with '0'. */
#define VG_MSG_LJUSTIFY  4 /* Must justify on the left. */
#define VG_MSG_PAREN     8 /* Parenthesize if present (for %y) */

/* Copy a string into the buffer. */
static UInt
myvprintf_str ( void(*send)(Char), Int flags, Int width, Char* str, 
                Bool capitalise )
{
#  define MAYBE_TOUPPER(ch) (capitalise ? VG_(toupper)(ch) : (ch))
   UInt ret = 0;
   Int i, extra;
   Int len = VG_(strlen)(str);

   if (width == 0) {
      ret += len;
      for (i = 0; i < len; i++)
         send(MAYBE_TOUPPER(str[i]));
      return ret;
   }

   if (len > width) {
      ret += width;
      for (i = 0; i < width; i++)
         send(MAYBE_TOUPPER(str[i]));
      return ret;
   }

   extra = width - len;
   if (flags & VG_MSG_LJUSTIFY) {
      ret += extra;
      for (i = 0; i < extra; i++)
         send(' ');
   }
   ret += len;
   for (i = 0; i < len; i++)
      send(MAYBE_TOUPPER(str[i]));
   if (!(flags & VG_MSG_LJUSTIFY)) {
      ret += extra;
      for (i = 0; i < extra; i++)
         send(' ');
   }

#  undef MAYBE_TOUPPER

   return ret;
}

/* Write P into the buffer according to these args:
 *  If SIGN is true, p is a signed.
 *  BASE is the base.
 *  If WITH_ZERO is true, '0' must be added.
 *  WIDTH is the width of the field.
 */
static UInt
myvprintf_int64 ( void(*send)(Char), Int flags, Int base, Int width, ULong p)
{
   Char buf[40];
   Int  ind = 0;
   Int  i;
   Bool neg = False;
   Char *digits = "0123456789ABCDEF";
   UInt ret = 0;

   if (base < 2 || base > 16)
      return ret;
 
   if ((flags & VG_MSG_SIGNED) && (Long)p < 0) {
      p   = - (Long)p;
      neg = True;
   }

   if (p == 0)
      buf[ind++] = '0';
   else {
      while (p > 0) {
         buf[ind++] = digits[p % base];
         p /= base;
       }
   }

   if (neg)
      buf[ind++] = '-';

   if (width > 0 && !(flags & VG_MSG_LJUSTIFY)) {
      for(; ind < width; ind++) {
         vg_assert(ind < 39);
         buf[ind] = (flags & VG_MSG_ZJUSTIFY) ? '0': ' ';
      }
   }

   /* Reverse copy to buffer.  */
   ret += ind;
   for (i = ind -1; i >= 0; i--)
      send(buf[i]);
   if (width > 0 && (flags & VG_MSG_LJUSTIFY)) {
      for(; ind < width; ind++) {
	 ret++;
         send((flags & VG_MSG_ZJUSTIFY) ? '0': ' ');
      }
   }
   return ret;
}


/* A simple vprintf().  */
UInt
VG_(vprintf) ( void(*send)(Char), const Char *format, va_list vargs )
{
   UInt ret = 0;
   int i;
   int flags;
   int width;
   Bool is_long;

   /* We assume that vargs has already been initialised by the 
      caller, using va_start, and that the caller will similarly
      clean up with va_end.
   */

   for (i = 0; format[i] != 0; i++) {
      if (format[i] != '%') {
         send(format[i]);
	 ret++;
         continue;
      }
      i++;
      /* A '%' has been found.  Ignore a trailing %. */
      if (format[i] == 0)
         break;
      if (format[i] == '%') {
         /* `%%' is replaced by `%'. */
         send('%');
	 ret++;
         continue;
      }
      flags = 0;
      is_long = False;
      width = 0; /* length of the field. */
      if (format[i] == '(') {
	 flags |= VG_MSG_PAREN;
	 i++;
      }
      /* If '-' follows '%', justify on the left. */
      if (format[i] == '-') {
         flags |= VG_MSG_LJUSTIFY;
         i++;
      }
      /* If '0' follows '%', pads will be inserted. */
      if (format[i] == '0') {
         flags |= VG_MSG_ZJUSTIFY;
         i++;
      }
      /* Compute the field length. */
      while (format[i] >= '0' && format[i] <= '9') {
         width *= 10;
         width += format[i++] - '0';
      }
      while (format[i] == 'l') {
         i++;
         is_long = True;
      }

      switch (format[i]) {
         case 'd': /* %d */
            flags |= VG_MSG_SIGNED;
            if (is_long)
               ret += myvprintf_int64(send, flags, 10, width, 
				      (ULong)(va_arg (vargs, Long)));
            else
               ret += myvprintf_int64(send, flags, 10, width, 
				      (ULong)(va_arg (vargs, Int)));
            break;
         case 'u': /* %u */
            if (is_long)
               ret += myvprintf_int64(send, flags, 10, width, 
				      (ULong)(va_arg (vargs, ULong)));
            else
               ret += myvprintf_int64(send, flags, 10, width, 
				      (ULong)(va_arg (vargs, UInt)));
            break;
         case 'p': /* %p */
	    ret += 2;
            send('0');
            send('x');
            ret += myvprintf_int64(send, flags, 16, width, 
				   (ULong)((UInt)va_arg (vargs, void *)));
            break;
         case 'x': /* %x */
            if (is_long)
               ret += myvprintf_int64(send, flags, 16, width, 
				      (ULong)(va_arg (vargs, ULong)));
            else
               ret += myvprintf_int64(send, flags, 16, width, 
				      (ULong)(va_arg (vargs, UInt)));
            break;
         case 'c': /* %c */
	    ret++;
            send(va_arg (vargs, int));
            break;
         case 's': case 'S': { /* %s */
            char *str = va_arg (vargs, char *);
            if (str == (char*) 0) str = "(null)";
            ret += myvprintf_str(send, flags, width, str, format[i]=='S');
            break;
	 }
	 case 'y': { /* %y - print symbol */
	    Char buf[100];
	    Char *cp = buf;
	    Addr a = va_arg(vargs, Addr);

	    if (flags & VG_MSG_PAREN)
	       *cp++ = '(';
	    if (VG_(get_fnname_w_offset)(a, cp, sizeof(buf)-4)) {
	       if (flags & VG_MSG_PAREN) {
		  cp += VG_(strlen)(cp);
		  *cp++ = ')';
		  *cp = '\0';
	       }
	       ret += myvprintf_str(send, flags, width, buf, 0);
	    }

	    break;
	 }
         default:
            break;
      }
   }
   return ret;
}


/* A general replacement for printf().  Note that only low-level 
   debugging info should be sent via here.  The official route is to
   to use vg_message().  This interface is deprecated.
*/
static char myprintf_buf[100];
static int  n_myprintf_buf;

static void add_to_myprintf_buf ( Char c )
{
   if (n_myprintf_buf >= 100-10 /*paranoia*/ ) {
      if (VG_(clo_logfile_fd) >= 0) {
         VG_(send_bytes_to_logging_sink)( 
            myprintf_buf, VG_(strlen)(myprintf_buf) );
      }
      n_myprintf_buf = 0;
      myprintf_buf[n_myprintf_buf] = 0;      
   }
   myprintf_buf[n_myprintf_buf++] = c;
   myprintf_buf[n_myprintf_buf] = 0;
}

UInt VG_(printf) ( const char *format, ... )
{
   UInt ret;
   va_list vargs;
   va_start(vargs,format);
   
   n_myprintf_buf = 0;
   myprintf_buf[n_myprintf_buf] = 0;      
   ret = VG_(vprintf) ( add_to_myprintf_buf, format, vargs );

   if (n_myprintf_buf > 0 && VG_(clo_logfile_fd) >= 0) {
      VG_(send_bytes_to_logging_sink)( myprintf_buf, n_myprintf_buf );
   }

   va_end(vargs);

   return ret;
}


/* A general replacement for sprintf(). */
UInt VG_(sprintf) ( Char* buf, Char *format, ... )
{
   Int ret;
   va_list vargs;
   Char *ptr = buf;
   static void add_to_vg_sprintf_buf ( Char c )
   {
      *ptr++ = c;
   }
   
   va_start(vargs,format);

   ret = VG_(vprintf) ( add_to_vg_sprintf_buf, format, vargs );
   add_to_vg_sprintf_buf(0);

   va_end(vargs);

   vg_assert(VG_(strlen)(buf) == ret);
   return ret;
}


/* ---------------------------------------------------------------------
   Misc str* functions.
   ------------------------------------------------------------------ */

Bool VG_(isspace) ( Char c )
{
   return (c == ' ' || c == '\n' || c == '\t' || c == 0);
}

Bool VG_(isdigit) ( Char c )
{
   return (c >= '0' && c <= '9');
}

Int VG_(strlen) ( const Char* str )
{
   Int i = 0;
   while (str[i] != 0) i++;
   return i;
}


Long VG_(atoll) ( Char* str )
{
   Bool neg = False;
   Long n = 0;
   if (*str == '-') { str++; neg = True; };
   while (*str >= '0' && *str <= '9') {
      n = 10*n + (Long)(*str - '0');
      str++;
   }
   if (neg) n = -n;
   return n;
}


Long VG_(atoll16) ( Char* str )
{
   Bool neg = False;
   Long n = 0;
   if (*str == '-') { str++; neg = True; };
   while (True) {
      if (*str >= '0' && *str <= '9') {
         n = 16*n + (Long)(*str - '0');
      }
      else 
      if (*str >= 'A' && *str <= 'F') {
         n = 16*n + (Long)((*str - 'A') + 10);
      }
      else 
      if (*str >= 'a' && *str <= 'f') {
         n = 16*n + (Long)((*str - 'a') + 10);
      }
      else {
	break;
      }
      str++;
   }
   if (neg) n = -n;
   return n;
}

Long VG_(atoll36) ( UInt base, Char* str )
{
   Bool neg = False;
   Long n = 0;
   vg_assert(base >= 2 && base <= 36);
   if (*str == '-') { str++; neg = True; };
   while (True) {
      if (*str >= '0' 
          && *str <= (Char)('9' - (10 - base))) {
         n = base*n + (Long)(*str - '0');
      }
      else 
      if (base > 10 && *str >= 'A' 
          && *str <= (Char)('Z' - (36 - base))) {
         n = base*n + (Long)((*str - 'A') + 10);
      }
      else 
      if (base > 10 && *str >= 'a' 
          && *str <= (Char)('z' - (36 - base))) {
         n = base*n + (Long)((*str - 'a') + 10);
      }
      else {
	break;
      }
      str++;
   }
   if (neg) n = -n;
   return n;
}


Char* VG_(strcat) ( Char* dest, const Char* src )
{
   Char* dest_orig = dest;
   while (*dest) dest++;
   while (*src) *dest++ = *src++;
   *dest = 0;
   return dest_orig;
}


Char* VG_(strncat) ( Char* dest, const Char* src, Int n )
{
   Char* dest_orig = dest;
   while (*dest) dest++;
   while (*src && n > 0) { *dest++ = *src++; n--; }
   *dest = 0;
   return dest_orig;
}


Char* VG_(strpbrk) ( const Char* s, const Char* accept )
{
   const Char* a;
   while (*s) {
      a = accept;
      while (*a)
         if (*a++ == *s)
            return (Char *) s;
      s++;
   }
   return NULL;
}


Char* VG_(strcpy) ( Char* dest, const Char* src )
{
   Char* dest_orig = dest;
   while (*src) *dest++ = *src++;
   *dest = 0;
   return dest_orig;
}


/* Copy bytes, not overrunning the end of dest and always ensuring
   zero termination. */
void VG_(strncpy_safely) ( Char* dest, const Char* src, Int ndest )
{
   Int i;
   vg_assert(ndest > 0);
   i = 0;
   dest[i] = 0;
   while (True) {
      if (src[i] == 0) return;
      if (i >= ndest-1) return;
      dest[i] = src[i];
      i++;
      dest[i] = 0;
   }
}


Char* VG_(strncpy) ( Char* dest, const Char* src, Int ndest )
{
   Int i = 0;
   while (True) {
      if (i >= ndest) return dest;     /* reached limit */
      dest[i] = src[i];
      if (src[i++] == 0) {
         /* reached NUL;  pad rest with zeroes as required */
         while (i < ndest) dest[i++] = 0;
         return dest;
      }
   }
}


Int VG_(strcmp) ( const Char* s1, const Char* s2 )
{
   while (True) {
      if (*s1 == 0 && *s2 == 0) return 0;
      if (*s1 == 0) return -1;
      if (*s2 == 0) return 1;

      if (*(UChar*)s1 < *(UChar*)s2) return -1;
      if (*(UChar*)s1 > *(UChar*)s2) return 1;

      s1++; s2++;
   }
}


Int VG_(strcmp_ws) ( const Char* s1, const Char* s2 )
{
   while (True) {
      if (VG_(isspace)(*s1) && VG_(isspace)(*s2)) return 0;
      if (VG_(isspace)(*s1)) return -1;
      if (VG_(isspace)(*s2)) return 1;

      if (*(UChar*)s1 < *(UChar*)s2) return -1;
      if (*(UChar*)s1 > *(UChar*)s2) return 1;

      s1++; s2++;
   }
}


Int VG_(strncmp) ( const Char* s1, const Char* s2, Int nmax )
{
   Int n = 0;
   while (True) {
      if (n >= nmax) return 0;
      if (*s1 == 0 && *s2 == 0) return 0;
      if (*s1 == 0) return -1;
      if (*s2 == 0) return 1;

      if (*(UChar*)s1 < *(UChar*)s2) return -1;
      if (*(UChar*)s1 > *(UChar*)s2) return 1;

      s1++; s2++; n++;
   }
}


Int VG_(strncmp_ws) ( const Char* s1, const Char* s2, Int nmax )
{
   Int n = 0;
   while (True) {
      if (n >= nmax) return 0;
      if (VG_(isspace)(*s1) && VG_(isspace)(*s2)) return 0;
      if (VG_(isspace)(*s1)) return -1;
      if (VG_(isspace)(*s2)) return 1;

      if (*(UChar*)s1 < *(UChar*)s2) return -1;
      if (*(UChar*)s1 > *(UChar*)s2) return 1;

      s1++; s2++; n++;
   }
}


Char* VG_(strstr) ( const Char* haystack, Char* needle )
{
   Int n; 
   if (haystack == NULL)
      return NULL;
   n = VG_(strlen)(needle);
   while (True) {
      if (haystack[0] == 0) 
         return NULL;
      if (VG_(strncmp)(haystack, needle, n) == 0) 
         return (Char*)haystack;
      haystack++;
   }
}


Char* VG_(strchr) ( const Char* s, Char c )
{
   while (True) {
      if (*s == c) return (Char*)s;
      if (*s == 0) return NULL;
      s++;
   }
}


void* VG_(memcpy) ( void *dest, const void *src, Int sz )
{
   const Char *s = (const Char *)src;
   Char *d = (Char *)dest;
   vg_assert(sz >= 0);

   while (sz--)
      *d++ = *s++;

   return dest;
}


void* VG_(memset) ( void *dest, Int c, Int sz )
{
   Char *d = (Char *)dest;
   vg_assert(sz >= 0);

   while (sz--)
      *d++ = c;

   return dest;
}

Int VG_(memcmp) ( const void* s1, const void* s2, Int n )
{
   Int res;
   UChar a0;
   UChar b0;
   vg_assert(n >= 0);

   while (n != 0) {
      a0 = ((UChar *) s1)[0];
      b0 = ((UChar *) s2)[0];
      s1 += 1;
      s2 += 1;
      res = a0 - b0;
      if (res != 0)
         return res;
      n -= 1;
   }
   return 0;
}

Char VG_(toupper) ( Char c )
{
   if (c >= 'a' && c <= 'z')
      return c + ('A' - 'a'); 
   else
      return c;
}


/* Inline just for the wrapper VG_(strdup) below */
__inline__ Char* VG_(arena_strdup) ( ArenaId aid, const Char* s )
{
   Int   i;
   Int   len = VG_(strlen)(s) + 1;
   Char* res = VG_(arena_malloc) (aid, len);
   for (i = 0; i < len; i++)
      res[i] = s[i];
   return res;
}

/* Wrapper to avoid exposing skins to ArenaId's */
Char* VG_(strdup) ( const Char* s )
{
   return VG_(arena_strdup) ( VG_AR_SKIN, s ); 
}

/* ---------------------------------------------------------------------
   A simple string matching routine, purloined from Hugs98.
      `*'    matches any sequence of zero or more characters
      `?'    matches any single character exactly 
      `\c'   matches the character c only (ignoring special chars)
      c      matches the character c only
   ------------------------------------------------------------------ */

/* Keep track of recursion depth. */
static Int recDepth;

static Bool string_match_wrk ( Char* pat, Char* str )
{
   vg_assert(recDepth >= 0 && recDepth < 250);
   recDepth++;
   for (;;) {
      switch (*pat) {
         case '\0' : return (*str=='\0');
         case '*'  : do {
                        if (string_match_wrk(pat+1,str)) {
                           recDepth--;
                           return True;
                        }
                     } while (*str++);
                     recDepth--;
                     return False;
         case '?'  : if (*str++=='\0') {
                        recDepth--;
                        return False;
                     }
                     pat++;
                     break;
         case '\\' : if (*++pat == '\0') {
                        recDepth--;
                        return False; /* spurious trailing \ in pattern */
                     }
                     /* falls through to ... */
         default   : if (*pat++ != *str++) {
                        recDepth--;
                        return False;
                     }
                     break;
      }
   }
}

Bool VG_(string_match) ( Char* pat, Char* str )
{
   Bool b;
   recDepth = 0;
   b = string_match_wrk ( pat, str );
   /*
   VG_(printf)("%s   %s   %s\n",
	       b?"TRUE ":"FALSE", pat, str);
   */
   return b;
}


/* ---------------------------------------------------------------------
   Assertery.
   ------------------------------------------------------------------ */

__attribute__ ((noreturn))
static void report_and_quit ( Char* report )
{
   VG_(pp_sched_status)();
   VG_(printf)("\n");
   VG_(printf)("Note: see also the FAQ.txt in the source distribution.\n");
   VG_(printf)("It contains workarounds to several common problems.\n");
   VG_(printf)("\n");
   VG_(printf)("If that doesn't help, please report this bug to: %s\n\n", 
               report);
   VG_(printf)("In the bug report, send all the above text, the valgrind\n");
   VG_(printf)("version, and what Linux distro you are using.  Thanks.\n\n");
   VG_(shutdown_logging)();
   VG_(exit)(1);
}

__attribute__ ((noreturn))
static void assert_fail ( Char* expr, Char* name, Char* report,
                          Char* file, Int line,   Char* fn )
{
   static Bool entered = False;
   if (entered) 
     VG_(exit)(2);
   entered = True;
   VG_(printf)("\n%s: %s:%d (%s): Assertion `%s' failed.\n",
               name, file, line, fn, expr );
   report_and_quit(report);
}

void VG_(skin_assert_fail) ( Char* expr, Char* file, Int line, Char* fn )
{
   assert_fail(expr, VG_(details).name, VG_(details).bug_reports_to, 
               file, line, fn);
}

void VG_(core_assert_fail) ( Char* expr, Char* file, Int line, Char* fn )
{
   assert_fail(expr, "valgrind", VG_EMAIL_ADDR, file, line, fn);
}

__attribute__ ((noreturn))
static void panic ( Char* name, Char* report, Char* str )
{
   VG_(printf)("\n%s: the `impossible' happened:\n   %s\n", name, str);
   VG_(printf)("Basic block ctr is approximately %llu\n", VG_(bbs_done) );
   report_and_quit(report);
}

void VG_(core_panic) ( Char* str )
{
   panic("valgrind", VG_EMAIL_ADDR, str);
}

void VG_(skin_panic) ( Char* str )
{
   panic(VG_(details).name, VG_(details).bug_reports_to, str);
}


/* ---------------------------------------------------------------------
   Primitive support for reading files.
   ------------------------------------------------------------------ */

/* Returns -1 on failure. */
Int VG_(open) ( const Char* pathname, Int flags, Int mode )
{  
   Int fd;

   /* (old comment, not sure if it still applies  NJN 2002-sep-09) */
   /* This gets a segmentation fault if pathname isn't a valid file.
      I don't know why.  It seems like the call to open is getting
      intercepted and messed with by glibc ... */
   /* fd = open( pathname, O_RDONLY ); */
   /* ... so we go direct to the horse's mouth, which seems to work
      ok: */
   fd = vg_do_syscall3(__NR_open, (UInt)pathname, flags, mode);
   /* VG_(printf)("result = %d\n", fd); */
   if (VG_(is_kerror)(fd)) fd = -1;
   return fd;
}

void VG_(close) ( Int fd )
{
   vg_do_syscall1(__NR_close, fd);
}


Int VG_(read) ( Int fd, void* buf, Int count)
{
   Int res;
   /* res = read( fd, buf, count ); */
   res = vg_do_syscall3(__NR_read, fd, (UInt)buf, count);
   if (VG_(is_kerror)(res)) res = -1;
   return res;
}

Int VG_(write) ( Int fd, void* buf, Int count)
{
   Int res;
   /* res = write( fd, buf, count ); */
   res = vg_do_syscall3(__NR_write, fd, (UInt)buf, count);
   if (VG_(is_kerror)(res)) res = -1;
   return res;
}

Int VG_(stat) ( Char* file_name, struct vki_stat* buf )
{
   Int res;
   res = vg_do_syscall2(__NR_stat, (UInt)file_name, (UInt)buf);
   return VG_(is_kerror)(res) ? (-1) : 0;
}

Int VG_(rename) ( Char* old_name, Char* new_name )
{
   Int res;
   res = vg_do_syscall2(__NR_rename, (UInt)old_name, (UInt)new_name);
   return VG_(is_kerror)(res) ? (-1) : 0;
}

Int VG_(unlink) ( Char* file_name )
{
   Int res;
   res = vg_do_syscall1(__NR_unlink, (UInt)file_name);
   return VG_(is_kerror)(res) ? (-1) : 0;
}

/* Nb: we do not allow the Linux extension which malloc()s memory for the
   buffer if buf==NULL, because we don't want Linux calling malloc() */
Char* VG_(getcwd) ( Char* buf, Int size )
{
   Int res;
   vg_assert(buf != NULL);
   res = vg_do_syscall2(__NR_getcwd, (UInt)buf, (UInt)size);
   return VG_(is_kerror)(res) ? ((Char*)NULL) : (Char*)res;
}


/* ---------------------------------------------------------------------
   Misc functions looking for a proper home.
   ------------------------------------------------------------------ */

/* We do getenv without libc's help by snooping around in
   VG_(client_envp) as determined at startup time. */
Char* VG_(getenv) ( Char* varname )
{
   Int i, n;
   n = VG_(strlen)(varname);
   for (i = 0; VG_(client_envp)[i] != NULL; i++) {
      Char* s = VG_(client_envp)[i];
      if (VG_(strncmp)(varname, s, n) == 0 && s[n] == '=') {
         return & s[n+1];
      }
   }
   return NULL;
}


/* You'd be amazed how many places need to know the current pid. */
Int VG_(getpid) ( void )
{
   Int res;
   /* res = getpid(); */
   res = vg_do_syscall0(__NR_getpid);
   return res;
}

Int VG_(getppid) ( void )
{
   Int res;
   res = vg_do_syscall0(__NR_getppid);
   return res;
}


/* Return -1 if error, else 0.  NOTE does not indicate return code of
   child! */
Int VG_(system) ( Char* cmd )
{
   Int pid, res;
   void* environ[1] = { NULL };
   if (cmd == NULL)
      return 1;
   pid = vg_do_syscall0(__NR_fork);
   if (VG_(is_kerror)(pid))
      return -1;
   if (pid == 0) {
      /* child */
      Char* argv[4];
      argv[0] = "/bin/sh";
      argv[1] = "-c";
      argv[2] = cmd;
      argv[3] = 0;
      (void)vg_do_syscall3(__NR_execve, 
                           (UInt)"/bin/sh", (UInt)argv, (UInt)&environ);
      /* If we're still alive here, execve failed. */
      return -1;
   } else {
      /* parent */
      res = vg_do_syscall3(__NR_waitpid, pid, (UInt)NULL, 0);
      if (VG_(is_kerror)(res)) {
         return -1;
      } else {
	return 0;
      }
   }
}


/* ---------------------------------------------------------------------
   Support for a millisecond-granularity counter using RDTSC.
   ------------------------------------------------------------------ */

static __inline__ ULong do_rdtsc_insn ( void )
{
   ULong x;
   __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
   return x;
}

/* 0 = pre-calibration, 1 = calibration, 2 = running */
static Int   rdtsc_calibration_state     = 0;
static ULong rdtsc_ticks_per_millisecond = 0; /* invalid value */

static struct vki_timeval rdtsc_cal_start_timeval;
static struct vki_timeval rdtsc_cal_end_timeval;

static ULong              rdtsc_cal_start_raw;
static ULong              rdtsc_cal_end_raw;

UInt VG_(read_millisecond_timer) ( void )
{
   ULong rdtsc_now;
   vg_assert(rdtsc_calibration_state == 2);
   rdtsc_now = do_rdtsc_insn();
   vg_assert(rdtsc_now > rdtsc_cal_end_raw);
   rdtsc_now -= rdtsc_cal_end_raw;
   rdtsc_now /= rdtsc_ticks_per_millisecond;
   return (UInt)rdtsc_now;
}


void VG_(start_rdtsc_calibration) ( void )
{
   Int res;
   vg_assert(rdtsc_calibration_state == 0);
   rdtsc_calibration_state = 1;
   rdtsc_cal_start_raw = do_rdtsc_insn();
   res = vg_do_syscall2(__NR_gettimeofday, (UInt)&rdtsc_cal_start_timeval, 
                                           (UInt)NULL);
   vg_assert(!VG_(is_kerror)(res));
}

void VG_(end_rdtsc_calibration) ( void )
{
   Int   res, loops;
   ULong cpu_clock_MHZ;
   ULong cal_clock_ticks;
   ULong cal_wallclock_microseconds;
   ULong wallclock_start_microseconds;
   ULong wallclock_end_microseconds;
   struct vki_timespec req;
   struct vki_timespec rem;
   
   vg_assert(rdtsc_calibration_state == 1);
   rdtsc_calibration_state = 2;

   /* Try and delay for 20 milliseconds, so that we can at least have
      some minimum level of accuracy. */
   req.tv_sec = 0;
   req.tv_nsec = 20 * 1000 * 1000;
   loops = 0;
   while (True) {
      res = VG_(nanosleep)(&req, &rem);
      vg_assert(res == 0 /*ok*/ || res == 1 /*interrupted*/);
      if (res == 0)
         break;
      if (rem.tv_sec == 0 && rem.tv_nsec == 0) 
         break;
      req = rem;
      loops++;
      if (loops > 100) 
         VG_(core_panic)("calibration nanosleep loop failed?!");
   }

   /* Now read both timers, and do the Math. */
   rdtsc_cal_end_raw = do_rdtsc_insn();
   res = vg_do_syscall2(__NR_gettimeofday, (UInt)&rdtsc_cal_end_timeval, 
                                           (UInt)NULL);

   vg_assert(rdtsc_cal_end_raw > rdtsc_cal_start_raw);
   cal_clock_ticks = rdtsc_cal_end_raw - rdtsc_cal_start_raw;

   wallclock_start_microseconds
      = (1000000ULL * (ULong)(rdtsc_cal_start_timeval.tv_sec)) 
         + (ULong)(rdtsc_cal_start_timeval.tv_usec);
   wallclock_end_microseconds
      = (1000000ULL * (ULong)(rdtsc_cal_end_timeval.tv_sec)) 
         + (ULong)(rdtsc_cal_end_timeval.tv_usec);
   vg_assert(wallclock_end_microseconds > wallclock_start_microseconds);
   cal_wallclock_microseconds 
      = wallclock_end_microseconds - wallclock_start_microseconds;

   /* Since we just nanoslept for 20 ms ... */
   vg_assert(cal_wallclock_microseconds >= 20000);

   /* Now we know (roughly) that cal_clock_ticks on RDTSC take
      cal_wallclock_microseconds elapsed time.  Calculate the RDTSC
      ticks-per-millisecond value. */
   if (0)
      VG_(printf)("%lld ticks in %lld microseconds\n", 
                  cal_clock_ticks,  cal_wallclock_microseconds );

   rdtsc_ticks_per_millisecond   
      = cal_clock_ticks / (cal_wallclock_microseconds / 1000ULL);
   cpu_clock_MHZ
      = (1000ULL * rdtsc_ticks_per_millisecond) / 1000000ULL;
   if (VG_(clo_verbosity) >= 1)
      VG_(message)(Vg_UserMsg, "Estimated CPU clock rate is %d MHz",
                               (UInt)cpu_clock_MHZ);
   if (cpu_clock_MHZ < 50 || cpu_clock_MHZ > 10000)
      VG_(core_panic)("end_rdtsc_calibration: "
                 "estimated CPU MHz outside range 50 .. 10000");
   /* Paranoia about division by zero later. */
   vg_assert(rdtsc_ticks_per_millisecond != 0);
   if (0)
      VG_(printf)("ticks per millisecond %llu\n", 
                  rdtsc_ticks_per_millisecond);
}



/* ---------------------------------------------------------------------
   Primitive support for bagging memory via mmap.
   ------------------------------------------------------------------ */

void* VG_(get_memory_from_mmap) ( Int nBytes, Char* who )
{
   static UInt tot_alloc = 0;
   void* p;
   p = VG_(mmap)( 0, nBytes,
                     VKI_PROT_READ|VKI_PROT_WRITE|VKI_PROT_EXEC, 
                     VKI_MAP_PRIVATE|VKI_MAP_ANONYMOUS, -1, 0 );
   if (p != ((void*)(-1))) {
      tot_alloc += (UInt)nBytes;
      if (0)
         VG_(printf)(
            "get_memory_from_mmap: %d tot, %d req = %p .. %p, caller %s\n",
            tot_alloc, nBytes, p, ((char*)p) + nBytes - 1, who );
      return p;
   }

   VG_(printf)("\n");
   VG_(printf)("VG_(get_memory_from_mmap): request for %d bytes failed.\n", 
               nBytes);
   VG_(printf)("VG_(get_memory_from_mmap): %d bytes already allocated.\n", 
               tot_alloc);
   VG_(printf)("\n");
   VG_(printf)("This may mean that you have run out of swap space,\n");
   VG_(printf)("since running programs on valgrind increases their memory\n");
   VG_(printf)("usage at least 3 times.  You might want to use 'top'\n");
   VG_(printf)("to determine whether you really have run out of swap.\n");
   VG_(printf)("If so, you may be able to work around it by adding a\n");
   VG_(printf)("temporary swap file -- this is easier than finding a\n");
   VG_(printf)("new swap partition.  Go ask your sysadmin(s) [politely!]\n");
   VG_(printf)("\n");
   VG_(printf)("VG_(get_memory_from_mmap): out of memory!  Fatal!  Bye!\n");
   VG_(printf)("\n");
   VG_(exit)(1);
}

/* ---------------------------------------------------------------------
   Generally useful...
   ------------------------------------------------------------------ */

Int VG_(log2) ( Int x ) 
{
   Int i;
   /* Any more than 32 and we overflow anyway... */
   for (i = 0; i < 32; i++) {
      if (1 << i == x) return i;
   }
   return -1;
}


/* ---------------------------------------------------------------------
   Gruesome hackery for connecting to a logging server over the network.
   This is all very Linux-kernel specific.
   ------------------------------------------------------------------ */

/* Various needed constants from the kernel iface (2.4),
   /usr/src/linux-2.4.9-31 */

/* kernel, ./include/linux/net.h */
#define SYS_SOCKET     1               /* sys_socket(2)        */
#define SYS_CONNECT    3               /* sys_connect(2)       */
#define SYS_SEND       9               /* sys_send(2)          */

typedef UInt __u32;

/* Internet address. */
struct vki_in_addr {
        __u32   s_addr;
};

/* kernel, include/linux/socket.h */
typedef unsigned short  vki_sa_family_t;
#define AF_INET                2       /* Internet IP Protocol        */
#define MSG_NOSIGNAL           0x4000  /* Do not generate SIGPIPE */

/* kernel, ./include/asm-i386/socket.h */
#define SOCK_STREAM 1                  /* stream (connection) socket  */

/* kernel, /usr/src/linux-2.4.9-31/linux/include/in.h */
/* Structure describing an Internet (IP) socket address. */
#define __SOCK_SIZE__   16              /* sizeof(struct sockaddr)      */
struct vki_sockaddr_in {
  vki_sa_family_t       sin_family;     /* Address family               */
  unsigned short int    sin_port;       /* Port number                  */
  struct vki_in_addr    sin_addr;       /* Internet address             */

  /* Pad to size of `struct sockaddr'. */
  unsigned char         __pad[__SOCK_SIZE__ - sizeof(short int) -
                        sizeof(unsigned short int) - 
                        sizeof(struct vki_in_addr)];
};


static
Int parse_inet_addr_and_port ( UChar* str, UInt* ip_addr, UShort* port );

static
Int my_socket ( Int domain, Int type, Int protocol );

static
Int my_connect ( Int sockfd, struct vki_sockaddr_in* serv_addr, 
                 Int addrlen );

static 
UInt my_htonl ( UInt x )
{
   return
      (((x >> 24) & 0xFF) << 0) | (((x >> 16) & 0xFF) << 8)
      | (((x >> 8) & 0xFF) << 16) | (((x >> 0) & 0xFF) << 24);
}

static
UShort my_htons ( UShort x )
{
   return
      (((x >> 8) & 0xFF) << 0) | (((x >> 0) & 0xFF) << 8);
}


/* The main function. 

   Supplied string contains either an ip address "192.168.0.1" or
   an ip address and port pair, "192.168.0.1:1500".  Parse these,
   and return:
     -1 if there is a parse error
     -2 if no parse error, but specified host:port cannot be opened
     the relevant file (socket) descriptor, otherwise.
 is used.
*/
Int VG_(connect_via_socket)( UChar* str )
{
   Int sd, res;
   struct vki_sockaddr_in servAddr;
   UInt   ip   = 0;
   UShort port = VG_CLO_DEFAULT_LOGPORT;
   Bool   ok   = parse_inet_addr_and_port(str, &ip, &port);
   if (!ok) 
      return -1;

   if (0)
      VG_(printf)("ip = %d.%d.%d.%d, port %d\n",
                  (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, 
                  (ip >> 8) & 0xFF, ip & 0xFF, 
	          (UInt)port );

   servAddr.sin_family = AF_INET;
   servAddr.sin_addr.s_addr = my_htonl(ip);
   servAddr.sin_port = my_htons(port);

   /* create socket */
   sd = my_socket(AF_INET, SOCK_STREAM, 0 /* IPPROTO_IP ? */);
   if (sd < 0) {
     /* this shouldn't happen ... nevertheless */
     return -2;
   }
			
   /* connect to server */
   res = my_connect(sd, (struct vki_sockaddr_in *) &servAddr, 
                        sizeof(servAddr));
   if (res < 0) {
     /* connection failed */
     return -2;
   }

   return sd;
}


/* Let d = one or more digits.  Accept either:
   d.d.d.d  or  d.d.d.d:d
*/
Int parse_inet_addr_and_port ( UChar* str, UInt* ip_addr, UShort* port )
{
#  define GET_CH ((*str) ? (*str++) : 0)
   UInt ipa, i, j, c, any;
   ipa = 0;
   for (i = 0; i < 4; i++) {
      j = 0;
      any = 0;
      while (1) {
         c = GET_CH; 
         if (c < '0' || c > '9') break;
         j = 10 * j + (int)(c - '0');
         any = 1;
      }
      if (any == 0 || j > 255) goto syntaxerr;
      ipa = (ipa << 8) + j;
      if (i <= 2 && c != '.') goto syntaxerr;
   }
   if (c == 0 || c == ':') 
      *ip_addr = ipa;
   if (c == 0) goto ok;
   if (c != ':') goto syntaxerr;
   j = 0;
   any = 0;
   while (1) {
      c = GET_CH; 
      if (c < '0' || c > '9') break;
      j = j * 10 + (int)(c - '0');
      any = 1;
      if (j > 65535) goto syntaxerr;
   }
   if (any == 0 || c != 0) goto syntaxerr;
   if (j < 1024) goto syntaxerr;
   *port = (UShort)j;
 ok:
   return 1;
 syntaxerr:
   return 0;
#  undef GET_CH
}


static
Int my_socket ( Int domain, Int type, Int protocol )
{
   Int res;
   UInt args[3];
   args[0] = domain;
   args[1] = type;
   args[2] = protocol;
   res = vg_do_syscall2(__NR_socketcall, SYS_SOCKET, (UInt)&args);
   if (VG_(is_kerror)(res)) 
      res = -1;
   return res;
}

static
Int my_connect ( Int sockfd, struct vki_sockaddr_in* serv_addr, 
                 Int addrlen )
{
   Int res;
   UInt args[3];
   args[0] = sockfd;
   args[1] = (UInt)serv_addr;
   args[2] = addrlen;
   res = vg_do_syscall2(__NR_socketcall, SYS_CONNECT, (UInt)&args);
   if (VG_(is_kerror)(res)) 
      res = -1;
   return res;
}

Int VG_(write_socket)( Int sd, void *msg, Int count )
{
   /* This is actually send(). */

   /* Requests not to send SIGPIPE on errors on stream oriented
      sockets when the other end breaks the connection. The EPIPE
      error is still returned. */
   Int flags = MSG_NOSIGNAL;

   Int res;
   UInt args[4];
   args[0] = sd;
   args[1] = (UInt)msg;
   args[2] = count;
   args[3] = flags;
   res = vg_do_syscall2(__NR_socketcall, SYS_SEND, (UInt)&args);
   if (VG_(is_kerror)(res)) 
      res = -1;
   return res;
}


/*--------------------------------------------------------------------*/
/*--- end                                              vg_mylibc.c ---*/
/*--------------------------------------------------------------------*/

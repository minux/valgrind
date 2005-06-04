
/*--------------------------------------------------------------------*/
/*--- Reimplementation of some C library stuff, to avoid depending ---*/
/*--- on libc.so.                                                  ---*/
/*---                                                  vg_mylibc.c ---*/
/*--------------------------------------------------------------------*/
 
/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2000-2005 Julian Seward 
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

#include "core.h"
#include "pub_core_aspacemgr.h"
#include "pub_core_debuglog.h"    /* VG_(debugLog_vprintf) */
#include "pub_core_libcbase.h"
#include "pub_core_libcassert.h"
#include "pub_core_libcprint.h"
#include "pub_core_main.h"
#include "pub_core_options.h"
#include "pub_core_stacktrace.h"
#include "pub_core_syscalls.h"
#include "pub_core_tooliface.h"
#include "vki_unistd.h"


/* ---------------------------------------------------------------------
   Wrappers around system calls, and other stuff, to do with signals.
   ------------------------------------------------------------------ */

/* sigemptyset, sigfullset, sigaddset and sigdelset return 0 on
   success and -1 on error.  
*/
Int VG_(sigfillset)( vki_sigset_t* set )
{
   Int i;
   if (set == NULL)
      return -1;
   for (i = 0; i < _VKI_NSIG_WORDS; i++)
      set->sig[i] = ~(UWord)0x0;
   return 0;
}

Int VG_(sigemptyset)( vki_sigset_t* set )
{
   Int i;
   if (set == NULL)
      return -1;
   for (i = 0; i < _VKI_NSIG_WORDS; i++)
      set->sig[i] = 0x0;
   return 0;
}

Bool VG_(isemptysigset)( const vki_sigset_t* set )
{
   Int i;
   vg_assert(set != NULL);
   for (i = 0; i < _VKI_NSIG_WORDS; i++)
      if (set->sig[i] != 0x0) return False;
   return True;
}

Bool VG_(isfullsigset)( const vki_sigset_t* set )
{
   Int i;
   vg_assert(set != NULL);
   for (i = 0; i < _VKI_NSIG_WORDS; i++)
      if (set->sig[i] != ~(UWord)0x0) return False;
   return True;
}

Bool VG_(iseqsigset)( const vki_sigset_t* set1, const vki_sigset_t* set2 )
{
   Int i;
   vg_assert(set1 != NULL && set2 != NULL);
   for (i = 0; i < _VKI_NSIG_WORDS; i++)
      if (set1->sig[i] != set2->sig[i]) return False;
   return True;
}


Int VG_(sigaddset)( vki_sigset_t* set, Int signum )
{
   if (set == NULL)
      return -1;
   if (signum < 1 || signum > _VKI_NSIG)
      return -1;
   signum--;
   set->sig[signum / _VKI_NSIG_BPW] |= (1UL << (signum % _VKI_NSIG_BPW));
   return 0;
}

Int VG_(sigdelset)( vki_sigset_t* set, Int signum )
{
   if (set == NULL)
      return -1;
   if (signum < 1 || signum > _VKI_NSIG)
      return -1;
   signum--;
   set->sig[signum / _VKI_NSIG_BPW] &= ~(1UL << (signum % _VKI_NSIG_BPW));
   return 0;
}

Int VG_(sigismember) ( const vki_sigset_t* set, Int signum )
{
   if (set == NULL)
      return 0;
   if (signum < 1 || signum > _VKI_NSIG)
      return 0;
   signum--;
   if (1 & ((set->sig[signum / _VKI_NSIG_BPW]) >> (signum % _VKI_NSIG_BPW)))
      return 1;
   else
      return 0;
}


/* Add all signals in src to dst. */
void VG_(sigaddset_from_set)( vki_sigset_t* dst, vki_sigset_t* src )
{
   Int i;
   vg_assert(dst != NULL && src != NULL);
   for (i = 0; i < _VKI_NSIG_WORDS; i++)
      dst->sig[i] |= src->sig[i];
}

/* Remove all signals in src from dst. */
void VG_(sigdelset_from_set)( vki_sigset_t* dst, vki_sigset_t* src )
{
   Int i;
   vg_assert(dst != NULL && src != NULL);
   for (i = 0; i < _VKI_NSIG_WORDS; i++)
      dst->sig[i] &= ~(src->sig[i]);
}


/* The functions sigaction, sigprocmask, sigpending and sigsuspend
   return 0 on success and -1 on error.  
*/
Int VG_(sigprocmask)( Int how, const vki_sigset_t* set, vki_sigset_t* oldset)
{
   Int res = VG_(do_syscall4)(__NR_rt_sigprocmask, 
                              how, (UWord)set, (UWord)oldset, 
                              _VKI_NSIG_WORDS * sizeof(UWord));
   return VG_(is_kerror)(res) ? -1 : 0;
}


Int VG_(sigaction) ( Int signum, const struct vki_sigaction* act,  
                     struct vki_sigaction* oldact)
{
   Int res = VG_(do_syscall4)(__NR_rt_sigaction,
		              signum, (UWord)act, (UWord)oldact, 
		              _VKI_NSIG_WORDS * sizeof(UWord));
   /* VG_(printf)("res = %d\n",res); */
   return VG_(is_kerror)(res) ? -1 : 0;
}


Int VG_(sigaltstack)( const vki_stack_t* ss, vki_stack_t* oss )
{
   Int res = VG_(do_syscall2)(__NR_sigaltstack, (UWord)ss, (UWord)oss);
   return VG_(is_kerror)(res) ? -1 : 0;
}

Int VG_(sigtimedwait)( const vki_sigset_t *set, vki_siginfo_t *info, 
                       const struct vki_timespec *timeout )
{
   Int res = VG_(do_syscall4)(__NR_rt_sigtimedwait, (UWord)set, (UWord)info, 
                              (UWord)timeout, sizeof(*set));

   return res;
}
 
Int VG_(signal)(Int signum, void (*sighandler)(Int))
{
   Int res;
   struct vki_sigaction sa;
   sa.ksa_handler = sighandler;
   sa.sa_flags = VKI_SA_ONSTACK | VKI_SA_RESTART;
   sa.sa_restorer = NULL;
   res = VG_(sigemptyset)( &sa.sa_mask );
   vg_assert(res == 0);
   res = VG_(do_syscall4)(__NR_rt_sigaction, signum, (UWord)&sa, (UWord)NULL,
			 _VKI_NSIG_WORDS * sizeof(UWord));
   return VG_(is_kerror)(res) ? -1 : 0;
}


Int VG_(kill)( Int pid, Int signo )
{
   Int res = VG_(do_syscall2)(__NR_kill, pid, signo);
   return VG_(is_kerror)(res) ? -1 : 0;
}


Int VG_(tkill)( ThreadId tid, Int signo )
{
   Int ret = -VKI_ENOSYS;

#if 0
   /* This isn't right because the client may create a process
      structure with multiple thread groups */
   ret = VG_(do_syscall)(__NR_tgkill, VG_(getpid)(), tid, signo);
#endif

   ret = VG_(do_syscall2)(__NR_tkill, tid, signo);

   if (ret == -VKI_ENOSYS)
      ret = VG_(do_syscall2)(__NR_kill, tid, signo);

   return VG_(is_kerror)(ret) ? -1 : 0;
}

Int VG_(sigpending) ( vki_sigset_t* set )
{
// Nb: AMD64/Linux doesn't have __NR_sigpending;  it only provides
// __NR_rt_sigpending.  This function will have to be abstracted in some
// way to account for this.  In the meantime, the easy option is to forget
// about it for AMD64 until it's needed.
#ifdef __amd64__
   I_die_here;
#else
   Int res = VG_(do_syscall1)(__NR_sigpending, (UWord)set);
   return VG_(is_kerror)(res) ? -1 : 0;
#endif
}

Int VG_(waitpid)(Int pid, Int *status, Int options)
{
   Int ret = VG_(do_syscall4)(__NR_wait4, pid, (UWord)status, options, 0);

   return VG_(is_kerror)(ret) ? -1 : ret;
}

Int VG_(gettid)(void)
{
   Int ret;

   ret = VG_(do_syscall0)(__NR_gettid);

   if (ret == -VKI_ENOSYS) {
      Char pid[16];
      
      /*
       * The gettid system call does not exist. The obvious assumption
       * to make at this point would be that we are running on an older
       * system where the getpid system call actually returns the ID of
       * the current thread.
       *
       * Unfortunately it seems that there are some systems with a kernel
       * where getpid has been changed to return the ID of the thread group
       * leader but where the gettid system call has not yet been added.
       *
       * So instead of calling getpid here we use readlink to see where
       * the /proc/self link is pointing...
       */
      if ((ret = VG_(do_syscall3)(__NR_readlink, (UWord)"/proc/self",
                                  (UWord)pid, sizeof(pid))) >= 0) 
      {
         pid[ret] = '\0';
         ret = VG_(atoll)(pid);
      }
   }

   return ret;
}



/* ---------------------------------------------------------------------
   mmap/munmap, exit, fcntl
   ------------------------------------------------------------------ */

void* VG_(mmap_native)(void *start, SizeT length, UInt prot, UInt flags,
                       UInt fd, OffT offset)
{
   UWord ret;
#if defined(VGP_x86_linux)
   { 
      UWord args[6];
      args[0] = (UWord)start;
      args[1] = length;
      args[2] = prot;
      args[3] = flags;
      args[4] = fd;
      args[5] = offset;
      ret = VG_(do_syscall1)(__NR_mmap, (UWord)args );
   }
#elif defined(VGP_amd64_linux)
   ret = VG_(do_syscall6)(__NR_mmap, (UWord)start, length, 
                         prot, flags, fd, offset);
#else
#  error Unknown platform
#endif
   return VG_(is_kerror)(ret) ? (void*)-1 : (void*)ret;
}

/* Returns -1 on failure. */
void* VG_(mmap)( void* start, SizeT length,
                 UInt prot, UInt flags, UInt sf_flags, UInt fd, OffT offset)
{
   Addr  res;

   if (!(flags & VKI_MAP_FIXED)) {
      start = (void *)VG_(find_map_space)((Addr)start, length, !!(flags & VKI_MAP_CLIENT));

      flags |= VKI_MAP_FIXED;
   }
   if (start == 0)
      return (void *)-1;

   res = (Addr)VG_(mmap_native)(start, length, prot, 
                                flags & ~(VKI_MAP_NOSYMS | VKI_MAP_CLIENT),
                                fd, offset);

   // Check it ended up in the right place.
   if (res != (Addr)-1) {
      if (flags & VKI_MAP_CLIENT) {
         vg_assert(VG_(client_base) <= res && res+length <= VG_(client_end));
      } else {
         vg_assert(VG_(valgrind_base) <= res && res+length-1 <= VG_(valgrind_last));
      }

      sf_flags |= SF_MMAP;
      if (  flags & VKI_MAP_FIXED)      sf_flags |= SF_FIXED;
      if (  flags & VKI_MAP_SHARED)     sf_flags |= SF_SHARED;
      if (!(flags & VKI_MAP_ANONYMOUS)) sf_flags |= SF_FILE;
      if (!(flags & VKI_MAP_CLIENT))    sf_flags |= SF_VALGRIND;
      if (  flags & VKI_MAP_NOSYMS)     sf_flags |= SF_NOSYMS;

      VG_(map_fd_segment)(res, length, prot, sf_flags, fd, offset, NULL);
   }

   return (void*)res;
}

static Int munmap_native(void *start, SizeT length)
{
   Int res = VG_(do_syscall2)(__NR_munmap, (UWord)start, length );
   return VG_(is_kerror)(res) ? -1 : 0;
}

/* Returns -1 on failure. */
Int VG_(munmap)( void* start, SizeT length )
{
   Int res = munmap_native(start, length);
   if (0 == res)
      VG_(unmap_range)((Addr)start, length);
   return res;
}

Int VG_(mprotect_native)( void *start, SizeT length, UInt prot )
{
   Int res = VG_(do_syscall3)(__NR_mprotect, (UWord)start, length, prot );
   return VG_(is_kerror)(res) ? -1 : 0;
}

Int VG_(mprotect)( void *start, SizeT length, UInt prot )
{
   Int res = VG_(mprotect_native)(start, length, prot);
   if (0 == res)
      VG_(mprotect_range)((Addr)start, length, prot);
   return res;
}

/* Pull down the entire world */
void VG_(exit)( Int status )
{
   (void)VG_(do_syscall1)(__NR_exit_group, status );
   (void)VG_(do_syscall1)(__NR_exit, status );
   /* Why are we still alive here? */
   /*NOTREACHED*/
   *(volatile Int *)0 = 'x';
   vg_assert(2+2 == 5);
}

/* Returns -1 on error. */
Int VG_(fcntl) ( Int fd, Int cmd, Int arg )
{
   Int res = VG_(do_syscall3)(__NR_fcntl, fd, cmd, arg);
   return VG_(is_kerror)(res) ? -1 : res;
}

Int VG_(poll)( struct vki_pollfd *ufds, UInt nfds, Int timeout)
{
   Int res = VG_(do_syscall3)(__NR_poll, (UWord)ufds, nfds, timeout);

   return res;
}


/* ---------------------------------------------------------------------
   strdup()
   ------------------------------------------------------------------ */

/* Inline just for the wrapper VG_(strdup) below */
__inline__ Char* VG_(arena_strdup) ( ArenaId aid, const Char* s )
{
   Int   i;
   Int   len;
   Char* res;

   if (s == NULL)
      return NULL;

   len = VG_(strlen)(s) + 1;
   res = VG_(arena_malloc) (aid, len);

   for (i = 0; i < len; i++)
      res[i] = s[i];
   return res;
}

/* Wrapper to avoid exposing tools to ArenaId's */
Char* VG_(strdup) ( const Char* s )
{
   return VG_(arena_strdup) ( VG_AR_TOOL, s ); 
}

/* ---------------------------------------------------------------------
   Misc functions looking for a proper home.
   ------------------------------------------------------------------ */

/* clone the environment */
static Char **env_clone ( Char **oldenv )
{
   Char **oldenvp;
   Char **newenvp;
   Char **newenv;
   Int  envlen;

   for (oldenvp = oldenv; oldenvp && *oldenvp; oldenvp++);

   envlen = oldenvp - oldenv + 1;
   
   newenv = VG_(arena_malloc)(VG_AR_CORE, envlen * sizeof(Char **));

   oldenvp = oldenv;
   newenvp = newenv;
   
   while (oldenvp && *oldenvp) {
      *newenvp++ = *oldenvp++;
   }
   
   *newenvp = *oldenvp;

   return newenv;
}

void  VG_(env_unsetenv) ( Char **env, const Char *varname )
{
   Char **from;
   Char **to = NULL;
   Int len = VG_(strlen)(varname);

   for(from = to = env; from && *from; from++) {
      if (!(VG_(strncmp)(varname, *from, len) == 0 && (*from)[len] == '=')) {
	 *to = *from;
	 to++;
      }
   }
   *to = *from;
}

/* set the environment; returns the old env if a new one was allocated */
Char **VG_(env_setenv) ( Char ***envp, const Char* varname, const Char *val )
{
   Char **env = (*envp);
   Char **cpp;
   Int len = VG_(strlen)(varname);
   Char *valstr = VG_(arena_malloc)(VG_AR_CORE, len + VG_(strlen)(val) + 2);
   Char **oldenv = NULL;

   VG_(sprintf)(valstr, "%s=%s", varname, val);

   for(cpp = env; cpp && *cpp; cpp++) {
      if (VG_(strncmp)(varname, *cpp, len) == 0 && (*cpp)[len] == '=') {
	 *cpp = valstr;
	 return oldenv;
      }
   }

   if (env == NULL) {
      env = VG_(arena_malloc)(VG_AR_CORE, sizeof(Char **) * 2);
      env[0] = valstr;
      env[1] = NULL;

      *envp = env;

   }  else {
      Int envlen = (cpp-env) + 2;
      Char **newenv = VG_(arena_malloc)(VG_AR_CORE, envlen * sizeof(Char **));

      for(cpp = newenv; *env; )
	 *cpp++ = *env++;
      *cpp++ = valstr;
      *cpp++ = NULL;

      oldenv = *envp;

      *envp = newenv;
   }

   return oldenv;
}

/* We do getenv without libc's help by snooping around in
   VG_(client_envp) as determined at startup time. */
Char *VG_(getenv)(Char *varname)
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

/* Support for getrlimit. */
Int VG_(getrlimit) (Int resource, struct vki_rlimit *rlim)
{
   Int res = -VKI_ENOSYS;
   /* res = getrlimit( resource, rlim ); */
#  ifdef __NR_ugetrlimit
   res = VG_(do_syscall2)(__NR_ugetrlimit, resource, (UWord)rlim);
#  endif
   if (res == -VKI_ENOSYS)
      res = VG_(do_syscall2)(__NR_getrlimit, resource, (UWord)rlim);
   if (VG_(is_kerror)(res)) res = -1;
   return res;
}


/* Support for setrlimit. */
Int VG_(setrlimit) (Int resource, const struct vki_rlimit *rlim)
{
   Int res;
   /* res = setrlimit( resource, rlim ); */
   res = VG_(do_syscall2)(__NR_setrlimit, resource, (UWord)rlim);
   if (VG_(is_kerror)(res)) res = -1;
   return res;
}


/* You'd be amazed how many places need to know the current pid. */
Int VG_(getpid) ( void )
{
   Int res;
   /* res = getpid(); */
   res = VG_(do_syscall0)(__NR_getpid);
   return res;
}

Int VG_(getpgrp) ( void )
{
   Int res;
   /* res = getpgid(); */
   res = VG_(do_syscall0)(__NR_getpgrp);
   return res;
}

Int VG_(getppid) ( void )
{
   Int res;
   res = VG_(do_syscall0)(__NR_getppid);
   return res;
}

Int VG_(setpgid) ( Int pid, Int pgrp )
{
   return VG_(do_syscall2)(__NR_setpgid, pid, pgrp);
}

/* Walk through a colon-separated environment variable, and remove the
   entries which match remove_pattern.  It slides everything down over
   the removed entries, and pads the remaining space with '\0'.  It
   modifies the entries in place (in the client address space), but it
   shouldn't matter too much, since we only do this just before an
   execve().

   This is also careful to mop up any excess ':'s, since empty strings
   delimited by ':' are considered to be '.' in a path.
*/
static void mash_colon_env(Char *varp, const Char *remove_pattern)
{
   Char *const start = varp;
   Char *entry_start = varp;
   Char *output = varp;

   if (varp == NULL)
      return;

   while(*varp) {
      if (*varp == ':') {
	 Char prev;
	 Bool match;

	 /* This is a bit subtle: we want to match against the entry
	    we just copied, because it may have overlapped with
	    itself, junking the original. */

	 prev = *output;
	 *output = '\0';

	 match = VG_(string_match)(remove_pattern, entry_start);

	 *output = prev;
	 
	 if (match) {
	    output = entry_start;
	    varp++;			/* skip ':' after removed entry */
	 } else
	    entry_start = output+1;	/* entry starts after ':' */
      }

      *output++ = *varp++;
   }

   /* match against the last entry */
   if (VG_(string_match)(remove_pattern, entry_start)) {
      output = entry_start;
      if (output > start) {
	 /* remove trailing ':' */
	 output--;
	 vg_assert(*output == ':');
      }
   }	 

   /* pad out the left-overs with '\0' */
   while(output < varp)
      *output++ = '\0';
}


// Removes all the Valgrind-added stuff from the passed environment.  Used
// when starting child processes, so they don't see that added stuff.
void VG_(env_remove_valgrind_env_stuff)(Char** envp)
{
   Int i;
   Char* ld_preload_str = NULL;
   Char* ld_library_path_str = NULL;
   Char* buf;

   // Find LD_* variables
   for (i = 0; envp[i] != NULL; i++) {
      if (VG_(strncmp)(envp[i], "LD_PRELOAD=", 11) == 0)
         ld_preload_str = &envp[i][11];
      if (VG_(strncmp)(envp[i], "LD_LIBRARY_PATH=", 16) == 0)
         ld_library_path_str = &envp[i][16];
   }

   buf = VG_(arena_malloc)(VG_AR_CORE, VG_(strlen)(VG_(libdir)) + 20);

   // Remove Valgrind-specific entries from LD_*.
   VG_(sprintf)(buf, "%s*/vg_inject.so", VG_(libdir));
   mash_colon_env(ld_preload_str, buf);
   VG_(sprintf)(buf, "%s*/vgpreload_*.so", VG_(libdir));
   mash_colon_env(ld_preload_str, buf);
   VG_(sprintf)(buf, "%s*", VG_(libdir));
   mash_colon_env(ld_library_path_str, buf);

   // Remove VALGRIND_CLO variable.
   VG_(env_unsetenv)(envp, VALGRINDCLO);

   // XXX if variable becomes empty, remove it completely?

   VG_(arena_free)(VG_AR_CORE, buf);
}

/* Return -1 if error, else 0.  NOTE does not indicate return code of
   child! */
Int VG_(system) ( Char* cmd )
{
   Int pid, res;
   if (cmd == NULL)
      return 1;
   pid = VG_(do_syscall0)(__NR_fork);
   if (VG_(is_kerror)(pid))
      return -1;
   if (pid == 0) {
      /* child */
      static Char** envp = NULL;
      Char* argv[4];

      /* restore the DATA rlimit for the child */
      VG_(setrlimit)(VKI_RLIMIT_DATA, &VG_(client_rlimit_data));

      envp = env_clone(VG_(client_envp));
      VG_(env_remove_valgrind_env_stuff)( envp ); 

      argv[0] = "/bin/sh";
      argv[1] = "-c";
      argv[2] = cmd;
      argv[3] = 0;

      (void)VG_(do_syscall3)(__NR_execve, 
                             (UWord)"/bin/sh", (UWord)argv, (UWord)envp);

      /* If we're still alive here, execve failed. */
      VG_(exit)(1);
   } else {
      /* parent */
      res = VG_(waitpid)(pid, NULL, 0);
      if (VG_(is_kerror)(res)) {
         return -1;
      } else {
	 return 0;
      }
   }
}


/* ---------------------------------------------------------------------
   Support for a millisecond-granularity timer.
   ------------------------------------------------------------------ */

UInt VG_(read_millisecond_timer) ( void )
{
   static ULong base = 0;
   struct vki_timeval tv_now;
   ULong now;
   Int res;

   res = VG_(do_syscall2)(__NR_gettimeofday, (UWord)&tv_now, (UWord)NULL);
   
   now = tv_now.tv_sec * 1000000ULL + tv_now.tv_usec;
   
   if (base == 0)
      base = now;

   return (now - base) / 1000;
}


void VG_(nanosleep)(struct vki_timespec *ts)
{
   VG_(do_syscall2)(__NR_nanosleep, (UWord)ts, (UWord)NULL);
}

/* ---------------------------------------------------------------------
   Primitive support for bagging memory via mmap.
   ------------------------------------------------------------------ */

void* VG_(get_memory_from_mmap) ( SizeT nBytes, Char* who )
{
   static SizeT tot_alloc = 0;
   void* p;
   p = VG_(mmap)(0, nBytes,
                 VKI_PROT_READ|VKI_PROT_WRITE|VKI_PROT_EXEC,
                 VKI_MAP_PRIVATE|VKI_MAP_ANONYMOUS, 0, -1, 0);

   if (p != ((void*)(-1))) {
      vg_assert((void*)VG_(valgrind_base) <= p && p <= (void*)VG_(valgrind_last));
      tot_alloc += nBytes;
      if (0)
         VG_(printf)(
            "get_memory_from_mmap: %llu tot, %llu req = %p .. %p, caller %s\n",
            (ULong)tot_alloc, (ULong)nBytes, p, ((char*)p) + nBytes - 1, who );
      return p;
   }

   VG_(printf)("\n");
   VG_(printf)("VG_(get_memory_from_mmap): %s's request for %llu bytes failed.\n",
               who, (ULong)nBytes);
   VG_(printf)("VG_(get_memory_from_mmap): %llu bytes already allocated.\n", 
               (ULong)tot_alloc);
   VG_(printf)("\n");
   VG_(printf)("Sorry.  You could try using a tool that uses less memory;\n");
   VG_(printf)("eg. addrcheck instead of memcheck.\n");
   VG_(printf)("\n");
   VG_(exit)(1);
}

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/

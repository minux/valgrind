
/*--------------------------------------------------------------------*/
/*--- 32-bit AIX5-specific kernel interface.      vki-ppc32-aix5.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2006-2007 OpenWorks LLP
      info@open-works.co.uk

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

/* This file defines types and constants for the kernel interface, and to
   make that clear everything is prefixed VKI_/vki_.
*/

/* This file was generated by running auxprogs/aix5_VKI_info.c. */

#ifndef __VKI_PPC32_AIX5_H
#define __VKI_PPC32_AIX5_H

#if !defined(VGP_ppc32_aix5)
#  error This file should be included in 32-bit AIX5 builds only.
#endif

//--------------------------------------------------------------
// VERIFIED
//--------------------------------------------------------------

/* ---------------- Errors ---------------- */

#define VKI_EINVAL 22
#define VKI_EINTR  4
#define VKI_ENOSYS 109
#define VKI_EAGAIN 11
#define VKI_ENOMEM 12
#define VKI_EACCES 13
#define VKI_EEXIST 17
#define VKI_EPERM  1
#define VKI_ENOENT 2
#define VKI_ESRCH  3
#define VKI_EBADF  9
#define VKI_EFAULT 14
#define VKI_EMFILE 24
#define VKI_ECHILD 10
#define VKI_EOVERFLOW 127
#define VKI_ERESTARTSYS 0 /* AIX doesn't have this */

/* ---------------- File I/O ---------------- */

#define VKI_O_WRONLY 0x00000001
#define VKI_O_RDONLY 0x00000000
#define VKI_O_APPEND 0x00000008
#define VKI_O_CREAT  0x00000100
#define VKI_O_RDWR   0x00000002
#define VKI_O_EXCL   0x00000400
#define VKI_O_TRUNC  0x00000200

#define VKI_S_IRUSR  0x00000100
#define VKI_S_IXUSR  0x00000040
#define VKI_S_IXGRP  0x00000008
#define VKI_S_IXOTH  0x00000001
#define VKI_S_IWUSR  0x00000080
#define VKI_S_ISUID  0x00000800
#define VKI_S_ISGID  0x00000400
#define VKI_S_IFMT   0x0000f000
#define VKI_S_IFDIR  0x00004000
#define VKI_S_IFCHR  0x00002000
#define VKI_S_IFBLK  0x00006000

/* Next 3 are from include/vki/vki-linux.h */
#define VKI_S_ISDIR(m)  (((m) & VKI_S_IFMT) == VKI_S_IFDIR)
#define VKI_S_ISCHR(m)  (((m) & VKI_S_IFMT) == VKI_S_IFCHR)
#define VKI_S_ISBLK(m)  (((m) & VKI_S_IFMT) == VKI_S_IFBLK)

#define VKI_F_DUPFD  0x00000000
#define VKI_F_SETFD  0x00000002
#define VKI_FD_CLOEXEC  0x00000001

#define VKI_R_OK 0x00000004
#define VKI_W_OK 0x00000002
#define VKI_X_OK 0x00000001

/* Known:
   sizeof(struct stat) = 116
     st_dev:  off  0 sz 4
     st_ino:  off  4 sz 4
     st_mode: off  8 sz 4
     st_uid:  off 16 sz 4
     st_gid:  off 20 sz 4
     st_size: off 28 sz 4
*/
struct vki_stat {
   /*  0 */ UInt st_dev;
   /*  4 */ UInt st_ino;
   /*  8 */ UInt st_mode;
   /* 12 */ UInt __off12;
   /* 16 */ UInt st_uid;
   /* 20 */ UInt st_gid;
   /* 24 */ UInt __off24;
   /* 28 */ UInt st_size;
   /* 32 */ UChar __off32[116-32];
};

#define VKI_STX_NORMAL  0

typedef UInt vki_size_t;

#define VKI_SEEK_SET 0
#define VKI_PATH_MAX 1023

/* Known:
   sizeof(struct iovec) = 8
     iov_base: off  0 sz 4
      iov_len: off  4 sz 4
*/
struct vki_iovec {
   /* 0 */ Addr iov_base;
   /* 4 */ UInt iov_len;
};

#define _VKI_IOC_NONE 0
#define _VKI_IOC_READ 1    /* kernel reads, userspace writes */
#define _VKI_IOC_WRITE 2   /* kernel writes, userspace reads */
#define _VKI_IOC_DIR(_x)   (((_x) >> 30) & 3)
#define _VKI_IOC_SIZE(_x)  (((_x) >> 16) & 0x7F)

/* ---------------- MMappery ---------------- */

/* This assumes the page size is 4096.  That assumption is checked
   by the launcher. */
#define VKI_PAGE_SHIFT  12
#define VKI_PAGE_SIZE   (1UL << VKI_PAGE_SHIFT)
#define VKI_MAX_PAGE_SHIFT      VKI_PAGE_SHIFT
#define VKI_MAX_PAGE_SIZE       VKI_PAGE_SIZE

#define VKI_PROT_NONE  0x00000000
#define VKI_PROT_READ  0x00000001
#define VKI_PROT_WRITE 0x00000002
#define VKI_PROT_EXEC  0x00000004

#define VKI_MAP_FIXED     0x00000100
#define VKI_MAP_PRIVATE   0x00000002
#define VKI_MAP_ANONYMOUS 0x00000010

/* ---------------- RLimitery ---------------- */

/* rlimit: these pertain to syscall "appgetrlimit" */
#define VKI_RLIMIT_DATA   0x00000002
#define VKI_RLIMIT_NOFILE 0x00000007
#define VKI_RLIMIT_STACK  0x00000003
#define VKI_RLIMIT_CORE   0x00000004

/* Known:
   sizeof(struct rlimit) = 8
     rlim_cur: off  0 sz 4
     rlim_max: off  4 sz 4
*/
struct vki_rlimit {
   UInt rlim_cur;
   UInt rlim_max;
};

/* ---------------- Time ---------------- */

/* Known:
   sizeof(struct timeval) = 8
      tv_sec: off  0 sz 4
     tv_usec: off  4 sz 4
*/
struct vki_timeval {
   UInt tv_sec;         /* seconds */
   UInt tv_usec;        /* microseconds */
};

/* Known:
   sizeof(struct timespec) = 8
      tv_sec: off  0 sz 4
     tv_nsec: off  4 sz 4
*/
struct vki_timespec {
   UInt tv_sec;         /* seconds */
   UInt tv_nsec;        /* nanoseconds */
};

/* ---------------- Signals ---------------- */

/* This layout verified 27 July 06. */
#define _VKI_NSIG_BPW   32
#define _VKI_NSIG       64
#define _VKI_NSIG_WORDS (_VKI_NSIG / _VKI_NSIG_BPW)

typedef struct {
   UInt sig[_VKI_NSIG_WORDS];
} vki_sigset_t;

#define VKI_SIGSEGV   11
#define VKI_SIGBUS    10
#define VKI_SIGFPE     8
#define VKI_SIGHUP     1
#define VKI_SIGINT     2
#define VKI_SIGQUIT    3
#define VKI_SIGABRT    6
#define VKI_SIGUSR1   30
#define VKI_SIGUSR2   31
#define VKI_SIGPIPE   13
#define VKI_SIGALRM   14
#define VKI_SIGTERM   15
/* VKI_SIGSTKFLT does not exist on AIX 5.2 */
#define VKI_SIGTTIN   21
#define VKI_SIGTTOU   22
#define VKI_SIGXCPU   24
#define VKI_SIGXFSZ   25
#define VKI_SIGVTALRM 34
#define VKI_SIGPROF   32
#define VKI_SIGIO     23
#define VKI_SIGPWR    29
/* VKI_SIGUNUSED does not exist on AIX 5.2 */
#define VKI_SIGRTMIN  50
#define VKI_SIGRTMAX  57
#define VKI_SIGTRAP    5
#define VKI_SIGCONT   19
#define VKI_SIGCHLD   20
#define VKI_SIGWINCH  28
#define VKI_SIGURG    16
#define VKI_SIGILL     4
#define VKI_SIGSTOP   17
#define VKI_SIGKILL    9
#define VKI_SIGTSTP   18
#define VKI_SIGSYS    12

/* Known:
    sizeof(struct sigaction) = 16
      sa_handler: off  0 sz 4
         sa_mask: off  4 sz 8
        sa_flags: off 12 sz 4
    sa_sigaction: off  0 sz 4
*/
struct vki_sigaction {
   void*        ksa_handler;
   vki_sigset_t sa_mask;
   UInt         sa_flags;
};

#define VKI_SA_ONSTACK      1
#define VKI_SA_RESTART      8
#define VKI_SA_RESETHAND    2
#define VKI_SA_SIGINFO    256
#define VKI_SA_NODEFER    512
#define VKI_SA_NOCLDSTOP    4
#define VKI_SA_NOCLDWAIT 1024

#define VKI_SA_RESTORER  0 /* AIX doesn't have this */
#define VKI_SA_NOMASK    0 /* AIX doesn't have this */
#define VKI_SA_ONESHOT   0 /* AIX doesn't have this */

#define VKI_SS_ONSTACK 1
#define VKI_SS_DISABLE 2

#define VKI_MINSIGSTKSZ 1168

#define VKI_SI_TKILL 0 /* AIX doesn't have this */
#define VKI_SI_USER  0 /* but it does have this */

#define VKI_SIG_BLOCK      0
#define VKI_SIG_SETMASK    2
#define VKI_SIG_UNBLOCK    1
#define VKI_SIG_IGN        (void*)1
#define VKI_SIG_DFL        (void*)0

#define VKI_SEGV_ACCERR 51
#define VKI_SEGV_MAPERR 50

#define VKI_TRAP_TRACE 61
#define VKI_BUS_OBJERR 3
#define VKI_BUS_ADRERR 2
#define VKI_BUS_ADRALN 1
#define VKI_FPE_FLTSUB 27
#define VKI_FPE_FLTINV 26
#define VKI_FPE_FLTRES 25
#define VKI_FPE_FLTUND 24
#define VKI_FPE_FLTOVF 23
#define VKI_FPE_FLTDIV 22
#define VKI_FPE_INTOVF 21
#define VKI_FPE_INTDIV 20
#define VKI_ILL_BADSTK 37
#define VKI_ILL_COPROC 36
#define VKI_ILL_PRVREG 35
#define VKI_ILL_PRVOPC 34
#define VKI_ILL_ILLTRP 33
#define VKI_ILL_ILLADR 32
#define VKI_ILL_ILLOPN 31
#define VKI_ILL_ILLOPC 30

/* Known: 
    sizeof(siginfo_t) = 64
     si_signo: off  0 sz 4
      si_code: off  8 sz 4
       si_pid: off 12 sz 4
      si_addr: off 20 sz 4
*/
typedef struct {
    UInt  si_signo;
    UInt  __off4;
    UInt  si_code;
    UInt  si_pid;
    UInt  __off16;
    void* si_addr;
    UInt  __off24;
    UInt  __off28;
    UInt  __off32;
    UInt  __off36;
    UInt  __off40;
    UInt  __off44;
    UInt  __off48;
    UInt  __off52;
    UInt  __off56;
    UInt  __off60;
} vki_siginfo_t;

/* Known:
   sizeof(stack_t) = 28
       ss_sp: off  0 sz 4
     ss_size: off  4 sz 4
    ss_flags: off  8 sz 4
*/
typedef struct vki_sigaltstack {
   /*  0 */ void* ss_sp;
   /*  4 */ UInt  ss_size;
   /*  8 */ UInt  ss_flags;
   /* 12 */ UInt  __off12;
   /* 16 */ UInt  __off16;
   /* 20 */ UInt  __off20;
   /* 24 */ UInt  __off24;
} vki_stack_t;

/* ---------------- Misc ---------------- */

#define VKI_PTRACE_TRACEME 0  /* nb: is really PT_TRACE_ME */
#define VKI_PTRACE_DETACH 31  /* nb: is really PT_DETACH */


//--------------------------------------------------------------
// BOGUS
//--------------------------------------------------------------

struct vki_dirent {
  int bogus;
};

struct vki_sockaddr {
  int bogus;
};

struct vki_pollfd {
  int bogus;
};

/* Structure describing an Internet (IP) socket address. */
//struct vki_sockaddr_in {
//  int bogus;
//};

struct vki_ucontext {
  int bogus;
};


//--------------------------------------------------------------
// FROM glibc-ports-2.4/sysdeps/unix/sysv/aix/dlldr.h
//--------------------------------------------------------------

/* Copyright (C) 2001 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */


/*

 int __loadx(flag, module, arg1, arg2, arg3)

 The __loadx() is a call to ld_loadutil() kernel function, which
 does the real work. Note ld_loadutil() is not exported an cannot be
 called directly from user space.

 void *ld_loadutil() call is a utility function used for loader extensions
 supporting run-time linking and dl*() functions.

 void *   - will return the modules entry point if it succeds of NULL
                on failure.

 int flag - the flag field performas a dual role: the top 8 bits specify
            the work for __loadx() to perform, the bottom 8 bits are
            used to pass flags to the work routines, all other bits are
            reserved.

*/

#define VKI_DL_LOAD       0x1000000 /* __loadx(flag,buf, buf_len, filename, libr_path) */
#define VKI_DL_POSTLOADQ  0x2000000 /* __loadx(flag,buf, buf_len, module_handle) */
#define VKI_DL_EXECQ      0x3000000 /* __loadx(flag,buf, buf_len) */
#define VKI_DL_EXITQ      0x4000000 /* __loadx(flag,buf, buf_len) */
#define VKI_DL_PREUNLOADQ 0x5000000 /* __loadx(flag,buf, buf_len, module_handle) */
#define VKI_DL_INIT       0x6000000 /* __loadx(flag,NULL) */
#define VKI_DL_GETSYM     0x7000000 /* __loadx(flag,symbol, index, modules_data_origin) */
#define VKI_DL_SETDEPEND  0x8000000 /* __loadx(flag,import_data_org, import_index, */
                                    /*              export_data_org, export_index) */
#define VKI_DL_DELDEPEND  0x9000000 /* __loadx(flag,import_data_org, import_index, */
                                    /*              export_data_org, export_index) */
#define VKI_DL_GLOBALSYM  0xA000000 /* __loadx(flag,symbol_name, ptr_to_rec_index, */
                                    /*                        ptr_to_rec_data_org) */
#define VKI_DL_UNIX_SYSCALL 0xB000000 /* __loadx(flag,syscall_symbol_name) */

#define VKI_DL_FUNCTION_MASK 0xFF000000
#define VKI_DL_SRCHDEPENDS   0x00100000
#define VKI_DL_SRCHMODULE    0x00080000
#define VKI_DL_SRCHLOADLIST  0x00040000
#define VKI_DL_LOAD_LDX1     0x00040000
#define VKI_DL_LOAD_RTL      0x00020000
#define VKI_DL_HASHSTRING    0x00020000
#define VKI_DL_INFO_OK       0x00010000
#define VKI_DL_LOAD_DLINFO   0x00010000
#define VKI_DL_UNLOADED      0x00020000


#endif // __VKI_PPC32_AIX5_H

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/

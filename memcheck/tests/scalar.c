#include "scalar.h"

int main(void)
{
   // uninitialised, but we know px[0] is 0x0
   long* px  = malloc(sizeof(long));
   long  x0  = px[0];

   // All __NR_xxx numbers are taken from x86
   
   // __NR_restart_syscall 1  XXX ???
   // (see below)

   // __NR_exit 1 
   // (see below)

   // __NR_fork 2

   // __NR_read 3 --> sys_read()
   // Nb: here we are also getting an error from the syscall arg itself.
   GO(__NR_read, "1+3s 1m");
   SY(__NR_read+x0, x0, x0, x0+1);

   // __NR_write 4 --> sys_write()
   GO(__NR_write, "3s 1m");
   SY(__NR_write, x0, x0, x0+1);

   // __NR_open 5 --> sys_open()
   GO(__NR_open, "(2-args) 2s 1m");
   SY(__NR_open, x0, x0, x0+1);

   GO(__NR_open, "(3-args) 1s 0m");
   SY(__NR_open, "tmp_write_file_foo", O_CREAT, x0);

   // __NR_close 6 --> sys_close()
   GO(__NR_close, "1s 0m");
   SY(__NR_close, x0-1);

   // __NR_waitpid 7 --> sys_waitpid()
   GO(__NR_waitpid, "3s 1m");
   SY(__NR_waitpid, x0-1);

   // __NR_creat 8 --> sys_creat()
   GO(__NR_creat, "2s 1m");
   SY(__NR_creat, x0, x0);

   // __NR_link 9 --> sys_link()
   GO(__NR_link, "2s 2m");
   SY(__NR_link, x0, x0);

   // __NR_unlink 10 --> sys_unlink()
   GO(__NR_unlink, "1s 1m");
   SY(__NR_unlink, x0);

   // __NR_execve 11 --> sys_execve()
   // Nb: could have 3 memory errors if we pass x0+1 as the 2nd and 3rd
   // args, except for bug #93174.
   GO(__NR_execve, "3s 1m");
   SY(__NR_execve, x0, x0, x0);

   // __NR_chdir 12 --> sys_chdir()
   GO(__NR_chdir, "1s 1m");
   SY(__NR_chdir, x0);

   // __NR_time 13 --> sys_time()
   GO(__NR_time, "1s 1m");
   SY(__NR_time, x0+1);

   // __NR_mknod 14 --> sys_mknod()
   GO(__NR_mknod, "3s 1m");
   SY(__NR_mknod, x0, x0, x0);

   // __NR_chmod 15 --> sys_chmod()
   GO(__NR_chmod, "2s 1m");
   SY(__NR_chmod, x0, x0);

   // __NR_lchown 16
   // (Not yet handled by Valgrind)

   // __NR_break 17 --> sys_ni_syscall()
   GO(__NR_break, "0e");
   SY(__NR_break);

   // __NR_oldstat 18
   // (obsolete, not handled by Valgrind)

   // __NR_lseek 19 --> sys_lseek()
   GO(__NR_lseek, "3s 0m");
   SY(__NR_lseek, x0, x0, x0);

   // __NR_getpid 20 --> sys_getpid()
   GO(__NR_getpid, "0s 0m");
   SY(__NR_getpid);

   // __NR_mount 21 --> sys_mount()
   GO(__NR_mount, "5s 3m");
   SY(__NR_mount, x0, x0, x0, x0, x0);
   
   // __NR_umount 22 --> sys_oldumount()
   GO(__NR_umount, "1s 1m");
   SY(__NR_umount, x0);

   // __NR_setuid 23 --> sys_setuid16()
   GO(__NR_setuid, "1s 0m");
   SY(__NR_setuid, x0);

   // __NR_getuid 24 --> sys_getuid16()
   GO(__NR_getuid, "0e");
   SY(__NR_getuid);

   // __NR_stime 25
   // (Not yet handled by Valgrind)

   // __NR_ptrace 26 --> arch/sys_ptrace()
   // XXX: memory pointed to be arg3 is never checked...
   GO(__NR_ptrace, "4s 2m");
   SY(__NR_ptrace, x0+PTRACE_GETREGS, x0, x0, x0);

   // __NR_alarm 27 --> sys_alarm()
   GO(__NR_alarm, "1s 0m");
   SY(__NR_alarm, x0);

   // __NR_oldfstat 28
   // (obsolete, not handled by Valgrind)

   // __NR_pause 29 --> sys_pause()
   // XXX: will have to be tested separately

   // __NR_utime 30 --> sys_utime()
   GO(__NR_utime, "2s 2m");
   SY(__NR_utime, x0, x0+1);

   // __NR_stty 31 --> sys_ni_syscall()
   GO(__NR_stty, "0e");
   SY(__NR_stty);

   // __NR_gtty 32 --> sys_ni_syscall()
   GO(__NR_gtty, "0e");
   SY(__NR_gtty);

   // __NR_access 33 --> sys_access()
   GO(__NR_access, "2s 1m");
   SY(__NR_access, x0, x0);

   // __NR_nice 34 --> sys_nice()
   GO(__NR_nice, "1s 0m");
   SY(__NR_nice, x0);

   // __NR_ftime 35 --> sys_ni_syscall()
   GO(__NR_ftime, "0e");
   SY(__NR_ftime);

   // __NR_sync 36 --> sys_sync()
   GO(__NR_sync, "0e");
   SY(__NR_sync);

   // __NR_kill 37 --> sys_kill()
   GO(__NR_kill, "2s 0m");
   SY(__NR_kill, x0, x0);

   // __NR_rename 38 --> sys_rename()
   GO(__NR_rename, "2s 2m");
   SY(__NR_rename, x0, x0);

   // __NR_mkdir 39 --> sys_mkdir()
   GO(__NR_mkdir, "2s 1m");
   SY(__NR_mkdir, x0, x0);

   // __NR_rmdir 40 --> sys_rmdir()
   GO(__NR_rmdir, "1s 1m");
   SY(__NR_rmdir, x0);

   // __NR_dup 41 --> sys_dup()
   GO(__NR_dup, "1s 0m");
   SY(__NR_dup, x0);

   // __NR_pipe 42 --> arch/sys_pipe()
   GO(__NR_pipe, "1s 1m");
   SY(__NR_pipe, x0);

   // __NR_times 43 --> sys_times()
   GO(__NR_times, "1s 1m");
   SY(__NR_times, x0);

   // __NR_prof 44 --> sys_ni_syscall()
   GO(__NR_prof, "0e");
   SY(__NR_prof);

   // __NR_brk 45 --> sys_brk()
   GO(__NR_brk, "1s 0m");
   SY(__NR_brk, x0);

   // __NR_setgid 46 --> sys_setgid16()
   GO(__NR_setgid, "1s 0m");
   SY(__NR_setgid);

   // __NR_getgid 47 --> sys_getgid16()
   GO(__NR_getgid, "0e");
   SY(__NR_getgid);

   // __NR_signal 48
   // (Not yet handled by Valgrind)

   // __NR_geteuid 49 --> sys_geteuid16()
   GO(__NR_geteuid, "0e");
   SY(__NR_geteuid);

   // __NR_getegid 50 --> sys_getegid16()
   GO(__NR_getegid, "0e");
   SY(__NR_getegid);

   // __NR_acct 51 --> sys_acct()
   GO(__NR_acct, "1s 1m");
   SY(__NR_acct, x0);

   // __NR_umount2 52 --> sys_umount()
   GO(__NR_umount2, "2s 1m");
   SY(__NR_umount2, x0, x0);

   // __NR_lock 53 --> sys_ni_syscall()
   GO(__NR_lock, "0e");
   SY(__NR_lock);

   // __NR_ioctl 54 --> sys_ioctl()
   #include <asm/ioctls.h>
   GO(__NR_ioctl, "3s 1m");
   SY(__NR_ioctl, x0, x0+TCSETS, x0);

   // __NR_fcntl 55 --> sys_fcntl()
   GO(__NR_fcntl, "3s 0m");
   SY(__NR_fcntl, x0, x0, x0);

   // __NR_mpx 56 --> sys_ni_syscall()
   GO(__NR_mpx, "0e");
   SY(__NR_mpx);

   // __NR_setpgid 57
   GO(__NR_setpgid, "2s 0m");
   SY(__NR_setpgid, x0, x0);

   // __NR_ulimit 58 --> sys_ni_syscall()
   GO(__NR_ulimit, "0e");
   SY(__NR_ulimit);

   // __NR_oldolduname 59
   // (obsolete, not handled by Valgrind)

   // __NR_umask 60
   GO(__NR_umask, "1s 0m");
   SY(__NR_umask, x0);

   // __NR_chroot 61
   GO(__NR_chroot, "1s 1m");
   SY(__NR_chroot, x0);

   // __NR_ustat 62
   // (deprecated, not handled by Valgrind)

   // __NR_dup2 63
   GO(__NR_dup2, "2s 0m");
   SY(__NR_dup2, x0, x0);

   // __NR_getppid 64 --> sys_getppid()
   GO(__NR_getppid, "0e");
   SY(__NR_getppid);

   // __NR_getpgrp 65 --> sys_getpgrp()
   GO(__NR_getpgrp, "0e");
   SY(__NR_getpgrp);

   // __NR_setsid 66 --> sys_setsid()
   GO(__NR_setsid, "0e");
   SY(__NR_setsid);

   // __NR_sigaction 67 --> sys_sigaction()
   GO(__NR_sigaction, "3s 2m");
   SY(__NR_sigaction, x0, x0+1, x0+1);

   // __NR_sgetmask 68
   // (Not yet handled by Valgrind)

   // __NR_ssetmask 69
   // (Not yet handled by Valgrind)

   // __NR_setreuid 70 --> sys_setreuid16()
   GO(__NR_setreuid, "2s 0m");
   SY(__NR_setreuid, x0, x0);

   // __NR_setregid 71 --> sys_setregid16()
   GO(__NR_setregid, "2s 0m");
   SY(__NR_setregid, x0, x0);

   // __NR_sigsuspend 72 --> sys_sigsuspend()

   // __NR_sigpending 73 --> sys_sigpending()
   GO(__NR_sigpending, "1s 1m");
   SY(__NR_sigpending, x0);

   // __NR_sethostname 74
   // (Not yet handled by Valgrind)

   // __NR_setrlimit 75 --> sys_setrlimit()
   GO(__NR_setrlimit, "2s 1m");
   SY(__NR_setrlimit, x0, x0);

   // __NR_getrlimit 76
   GO(__NR_getrlimit, "2s 1m");
   SY(__NR_getrlimit, x0, x0);

   // __NR_getrusage 77
 //GO(__NR_getrusage, ".s .m");
 //SY(__NR_getrusage);

   // __NR_gettimeofday 78
 //GO(__NR_gettimeofday, ".s .m");
 //SY(__NR_gettimeofday);

   // __NR_settimeofday 79
 //GO(__NR_settimeofday, ".s .m");
 //SY(__NR_settimeofday);

   // __NR_getgroups 80
 //GO(__NR_getgroups, ".s .m");
 //SY(__NR_getgroups);

   // __NR_setgroups 81
 //GO(__NR_setgroups, ".s .m");
 //SY(__NR_setgroups);

   // __NR_select 82
 //GO(__NR_select, ".s .m");
 //SY(__NR_select);

   // __NR_symlink 83
 //GO(__NR_symlink, ".s .m");
 //SY(__NR_symlink);

   // __NR_oldlstat 84
   // (obsolete, not handled by Valgrind)

   // __NR_readlink 85
 //GO(__NR_readlink, ".s .m");
 //SY(__NR_readlink);

   // __NR_uselib 86
   // (Not yet handled by Valgrind)

   // __NR_swapon 87
   // (Not yet handled by Valgrind)

   // __NR_reboot 88
   // (Not yet handled by Valgrind)

   // __NR_readdir 89
   // (superseded, not handled by Valgrind)

   // __NR_mmap 90
 //GO(__NR_mmap, ".s .m");
 //SY(__NR_mmap);

   // __NR_munmap 91
 //GO(__NR_munmap, ".s .m");
 //SY(__NR_munmap);

   // __NR_truncate 92
 //GO(__NR_truncate, ".s .m");
 //SY(__NR_truncate);

   // __NR_ftruncate 93
 //GO(__NR_ftruncate, ".s .m");
 //SY(__NR_ftruncate);

   // __NR_fchmod 94
 //GO(__NR_fchmod, ".s .m");
 //SY(__NR_fchmod);

   // __NR_fchown 95
 //GO(__NR_fchown, ".s .m");
 //SY(__NR_fchown);

   // __NR_getpriority 96
 //GO(__NR_getpriority, ".s .m");
 //SY(__NR_getpriority);

   // __NR_setpriority 97
 //GO(__NR_setpriority, ".s .m");
 //SY(__NR_setpriority);

   // __NR_profil 98
 //GO(__NR_profil, ".s .m");
 //SY(__NR_profil);

   // __NR_statfs 99
 //GO(__NR_statfs, ".s .m");
 //SY(__NR_statfs);

   // __NR_fstatfs 100
 //GO(__NR_fstatfs, ".s .m");
 //SY(__NR_fstatfs);

   // __NR_ioperm 101
 //GO(__NR_ioperm, ".s .m");
 //SY(__NR_ioperm);

   // __NR_socketcall 102
 //GO(__NR_socketcall, ".s .m");
 //SY(__NR_socketcall);

   // __NR_syslog 103
 //GO(__NR_syslog, ".s .m");
 //SY(__NR_syslog);

   // __NR_setitimer 104
 //GO(__NR_setitimer, ".s .m");
 //SY(__NR_setitimer);

   // __NR_getitimer 105
 //GO(__NR_getitimer, ".s .m");
 //SY(__NR_getitimer);

   // __NR_stat 106
 //GO(__NR_stat, ".s .m");
 //SY(__NR_stat);

   // __NR_lstat 107
 //GO(__NR_lstat, ".s .m");
 //SY(__NR_lstat);

   // __NR_fstat 108
 //GO(__NR_fstat, ".s .m");
 //SY(__NR_fstat);

   // __NR_olduname 109
   // (obsolete, not handled by Valgrind)

   // __NR_iopl 110
 //GO(__NR_iopl, ".s .m");
 //SY(__NR_iopl);

   // __NR_vhangup 111 --> sys_vhangup()
   GO(__NR_vhangup, "0e");
   SY(__NR_vhangup);
   
   // __NR_idle 112 --> sys_ni_syscall()
   GO(__NR_idle, "0e");
   SY(__NR_idle);

   // __NR_vm86old 113
 //GO(__NR_vm86old, ".s .m");
 //SY(__NR_vm86old);

   // __NR_wait4 114
 //GO(__NR_wait4, ".s .m");
 //SY(__NR_wait4);

   // __NR_swapoff 115
 //GO(__NR_swapoff, ".s .m");
 //SY(__NR_swapoff);

   // __NR_sysinfo 116
 //GO(__NR_sysinfo, ".s .m");
 //SY(__NR_sysinfo);

   // __NR_ipc 117
 //GO(__NR_ipc, ".s .m");
 //SY(__NR_ipc);

   // __NR_fsync 118
 //GO(__NR_fsync, ".s .m");
 //SY(__NR_fsync);

   // __NR_sigreturn 119
 //GO(__NR_sigreturn, ".s .m");
 //SY(__NR_sigreturn);

   // __NR_clone 120
 //GO(__NR_clone, ".s .m");
 //SY(__NR_clone);

   // __NR_setdomainname 121
 //GO(__NR_setdomainname, ".s .m");
 //SY(__NR_setdomainname);

   // __NR_uname 122
   GO(__NR_uname, "1s 1m");
   SY(__NR_uname, x0);

   // __NR_modify_ldt 123
 //GO(__NR_modify_ldt, ".s .m");
 //SY(__NR_modify_ldt);

   // __NR_adjtimex 124
 //GO(__NR_adjtimex, ".s .m");
 //SY(__NR_adjtimex);

   // __NR_mprotect 125
 //GO(__NR_mprotect, ".s .m");
 //SY(__NR_mprotect);

   // __NR_sigprocmask 126
 //GO(__NR_sigprocmask, ".s .m");
 //SY(__NR_sigprocmask);

   // __NR_create_module 127 --> sys_ni_syscall()
   GO(__NR_create_module, "0e");
   SY(__NR_create_module);

   // __NR_init_module 128
 //GO(__NR_init_module, ".s .m");
 //SY(__NR_init_module);

   // __NR_delete_module 129
 //GO(__NR_delete_module, ".s .m");
 //SY(__NR_delete_module);

   // __NR_get_kernel_syms 130 --> sys_ni_syscall()
   GO(__NR_get_kernel_syms, "0e");
   SY(__NR_get_kernel_syms);

   // __NR_quotactl 131
 //GO(__NR_quotactl, ".s .m");
 //SY(__NR_quotactl);

   // __NR_getpgid 132
 //GO(__NR_getpgid, ".s .m");
 //SY(__NR_getpgid);

   // __NR_fchdir 133
 //GO(__NR_fchdir, ".s .m");
 //SY(__NR_fchdir);

   // __NR_bdflush 134
 //GO(__NR_bdflush, ".s .m");
 //SY(__NR_bdflush);

   // __NR_sysfs 135
 //GO(__NR_sysfs, ".s .m");
 //SY(__NR_sysfs);

   // __NR_personality 136
 //GO(__NR_personality, ".s .m");
 //SY(__NR_personality);

   // __NR_afs_syscall 137 --> sys_ni_syscall()
   GO(__NR_afs_syscall, "0e");
   SY(__NR_afs_syscall);

   // __NR_setfsuid 138
 //GO(__NR_setfsuid, ".s .m");
 //SY(__NR_setfsuid);

   // __NR_setfsgid 139
 //GO(__NR_setfsgid, ".s .m");
 //SY(__NR_setfsgid);

   // __NR__llseek 140
 //GO(__NR__llseek, ".s .m");
 //SY(__NR__llseek);

   // __NR_getdents 141
 //GO(__NR_getdents, ".s .m");
 //SY(__NR_getdents);

   // __NR__newselect 142
 //GO(__NR__newselect, ".s .m");
 //SY(__NR__newselect);

   // __NR_flock 143
 //GO(__NR_flock, ".s .m");
 //SY(__NR_flock);

   // __NR_msync 144
 //GO(__NR_msync, ".s .m");
 //SY(__NR_msync);

   // __NR_readv 145
 //GO(__NR_readv, ".s .m");
 //SY(__NR_readv);

   // __NR_writev 146
 //GO(__NR_writev, ".s .m");
 //SY(__NR_writev);

   // __NR_getsid 147
 //GO(__NR_getsid, ".s .m");
 //SY(__NR_getsid);

   // __NR_fdatasync 148
 //GO(__NR_fdatasync, ".s .m");
 //SY(__NR_fdatasync);

   // __NR__sysctl 149
 //GO(__NR__sysctl, ".s .m");
 //SY(__NR__sysctl);

   // __NR_mlock 150
 //GO(__NR_mlock, ".s .m");
 //SY(__NR_mlock);

   // __NR_munlock 151
 //GO(__NR_munlock, ".s .m");
 //SY(__NR_munlock);

   // __NR_mlockall 152
 //GO(__NR_mlockall, ".s .m");
 //SY(__NR_mlockall);

   // __NR_munlockall 153 --> sys_munlockall()
   GO(__NR_munlockall, "0e");
   SY(__NR_munlockall);

   // __NR_sched_setparam 154
 //GO(__NR_sched_setparam, ".s .m");
 //SY(__NR_sched_setparam);

   // __NR_sched_getparam 155
 //GO(__NR_sched_getparam, ".s .m");
 //SY(__NR_sched_getparam);

   // __NR_sched_setscheduler 156
 //GO(__NR_sched_setscheduler, ".s .m");
 //SY(__NR_sched_setscheduler);

   // __NR_sched_getscheduler 157
 //GO(__NR_sched_getscheduler, ".s .m");
 //SY(__NR_sched_getscheduler);

   // __NR_sched_yield 158
 //GO(__NR_sched_yield, ".s .m");
 //SY(__NR_sched_yield);

   // __NR_sched_get_priority_max 159
 //GO(__NR_sched_get_priority_max, ".s .m");
 //SY(__NR_sched_get_priority_max);

   // __NR_sched_get_priority_min 160
 //GO(__NR_sched_get_priority_min, ".s .m");
 //SY(__NR_sched_get_priority_min);

   // __NR_sched_rr_get_interval 161
 //GO(__NR_sched_rr_get_interval, ".s .m");
 //SY(__NR_sched_rr_get_interval);

   // __NR_nanosleep 162
 //GO(__NR_nanosleep, ".s .m");
 //SY(__NR_nanosleep);

   // __NR_mremap 163
 //GO(__NR_mremap, ".s .m");
 //SY(__NR_mremap);

   // __NR_setresuid 164
 //GO(__NR_setresuid, ".s .m");
 //SY(__NR_setresuid);

   // __NR_getresuid 165
 //GO(__NR_getresuid, ".s .m");
 //SY(__NR_getresuid);

   // __NR_vm86 166
 //GO(__NR_vm86, ".s .m");
 //SY(__NR_vm86);

   // __NR_query_module 167 --> sys_ni_syscall()
   GO(__NR_query_module, "0e");
   SY(__NR_query_module);

   // __NR_poll 168
 //GO(__NR_poll, ".s .m");
 //SY(__NR_poll);

   // __NR_nfsservctl 169
 //GO(__NR_nfsservctl, ".s .m");
 //SY(__NR_nfsservctl);

   // __NR_setresgid 170
 //GO(__NR_setresgid, ".s .m");
 //SY(__NR_setresgid);

   // __NR_getresgid 171
 //GO(__NR_getresgid, ".s .m");
 //SY(__NR_getresgid);

   // __NR_prctl              172
 //GO(__NR_prctl, ".s .m");
 //SY(__NR_prctl);

   // __NR_rt_sigreturn 173
 //GO(__NR_rt_sigreturn, ".s .m");
 //SY(__NR_rt_sigreturn);

   // __NR_rt_sigaction 174
 //GO(__NR_rt_sigaction, ".s .m");
 //SY(__NR_rt_sigaction);

   // __NR_rt_sigprocmask 175
 //GO(__NR_rt_sigprocmask, ".s .m");
 //SY(__NR_rt_sigprocmask);

   // __NR_rt_sigpending 176
 //GO(__NR_rt_sigpending, ".s .m");
 //SY(__NR_rt_sigpending);

   // __NR_rt_sigtimedwait 177
 //GO(__NR_rt_sigtimedwait, ".s .m");
 //SY(__NR_rt_sigtimedwait);

   // __NR_rt_sigqueueinfo 178
 //GO(__NR_rt_sigqueueinfo, ".s .m");
 //SY(__NR_rt_sigqueueinfo);

   // __NR_rt_sigsuspend 179
 //GO(__NR_rt_sigsuspend, ".s .m");
 //SY(__NR_rt_sigsuspend);

   // __NR_pread64 180
 //GO(__NR_pread64, ".s .m");
 //SY(__NR_pread64);

   // __NR_pwrite64 181
 //GO(__NR_pwrite64, ".s .m");
 //SY(__NR_pwrite64);

   // __NR_chown 182
 //GO(__NR_chown, ".s .m");
 //SY(__NR_chown);

   // __NR_getcwd 183
 //GO(__NR_getcwd, ".s .m");
 //SY(__NR_getcwd);

   // __NR_capget 184
 //GO(__NR_capget, ".s .m");
 //SY(__NR_capget);

   // __NR_capset 185
 //GO(__NR_capset, ".s .m");
 //SY(__NR_capset);

   // __NR_sigaltstack 186
 //GO(__NR_sigaltstack, ".s .m");
 //SY(__NR_sigaltstack);

   // __NR_sendfile 187
 //GO(__NR_sendfile, ".s .m");
 //SY(__NR_sendfile);

   // __NR_getpmsg 188
 //GO(__NR_getpmsg, ".s .m");
 //SY(__NR_getpmsg);

   // __NR_putpmsg 189
 //GO(__NR_putpmsg, ".s .m");
 //SY(__NR_putpmsg);

   // __NR_vfork 190
 //GO(__NR_vfork, ".s .m");
 //SY(__NR_vfork);

   // __NR_ugetrlimit 191
   GO(__NR_ugetrlimit, "2s 1m");
   SY(__NR_ugetrlimit, x0, x0);

   // __NR_mmap2 192
 //GO(__NR_mmap2, ".s .m");
 //SY(__NR_mmap2);

   // __NR_truncate64 193
 //GO(__NR_truncate64, ".s .m");
 //SY(__NR_truncate64);

   // __NR_ftruncate64 194
 //GO(__NR_ftruncate64, ".s .m");
 //SY(__NR_ftruncate64);

   // __NR_stat64 195
 //GO(__NR_stat64, ".s .m");
 //SY(__NR_stat64);

   // __NR_lstat64 196
 //GO(__NR_lstat64, ".s .m");
 //SY(__NR_lstat64);

   // __NR_fstat64 197
 //GO(__NR_fstat64, ".s .m");
 //SY(__NR_fstat64);

   // __NR_lchown32 198
 //GO(__NR_lchown32, ".s .m");
 //SY(__NR_lchown32);

   // __NR_getuid32 199 --> sys_getuid()
   GO(__NR_getuid32, "0e");
   SY(__NR_getuid32);

   // __NR_getgid32 200 --> sys_getgid()
   GO(__NR_getgid32, "0e");
   SY(__NR_getgid32);

   // __NR_geteuid32 201 --> sys_geteuid()
   GO(__NR_geteuid32, "0e");
   SY(__NR_geteuid32);

   // __NR_getegid32 202 --> sys_getegid()
   GO(__NR_getegid32, "0e");
   SY(__NR_getegid32);

   // __NR_setreuid32 203
   GO(__NR_setreuid32, "2s 0m");
   SY(__NR_setreuid32, x0, x0);

   // __NR_setregid32 204
   GO(__NR_setregid32, "2s 0m");
   SY(__NR_setregid32, x0, x0);

   // __NR_getgroups32 205
 //GO(__NR_getgroups32, ".s .m");
 //SY(__NR_getgroups32);

   // __NR_setgroups32 206
 //GO(__NR_setgroups32, ".s .m");
 //SY(__NR_setgroups32);

   // __NR_fchown32 207
 //GO(__NR_fchown32, ".s .m");
 //SY(__NR_fchown32);

   // __NR_setresuid32 208
 //GO(__NR_setresuid32, ".s .m");
 //SY(__NR_setresuid32);

   // __NR_getresuid32 209
 //GO(__NR_getresuid32, ".s .m");
 //SY(__NR_getresuid32);

   // __NR_setresgid32 210
 //GO(__NR_setresgid32, ".s .m");
 //SY(__NR_setresgid32);

   // __NR_getresgid32 211
 //GO(__NR_getresgid32, ".s .m");
 //SY(__NR_getresgid32);

   // __NR_chown32 212
 //GO(__NR_chown32, ".s .m");
 //SY(__NR_chown32);

   // __NR_setuid32 213 --> sys_setuid()
   GO(__NR_setuid32, "1s 0m");
   SY(__NR_setuid32, x0);

   // __NR_setgid32 214
   GO(__NR_setgid32, "1s 0m");
   SY(__NR_setgid32);

   // __NR_setfsuid32 215
 //GO(__NR_setfsuid32, ".s .m");
 //SY(__NR_setfsuid32);

   // __NR_setfsgid32 216
 //GO(__NR_setfsgid32, ".s .m");
 //SY(__NR_setfsgid32);

   // __NR_pivot_root 217
 //GO(__NR_pivot_root, ".s .m");
 //SY(__NR_pivot_root);

   // __NR_mincore 218
 //GO(__NR_mincore, ".s .m");
 //SY(__NR_mincore);

   // __NR_madvise 219
 //GO(__NR_madvise, ".s .m");
 //SY(__NR_madvise);

   // __NR_getdents64 220
 //GO(__NR_getdents64, ".s .m");
 //SY(__NR_getdents64);

   // __NR_fcntl64 221
 //GO(__NR_fcntl64, ".s .m");
 //SY(__NR_fcntl64);

   // 222 --> sys_ni_syscall()
   GO(222, "0e");
   SY(222);

   // 223 --> sys_ni_syscall()
   GO(223, "0e");
   SY(223);

   // __NR_gettid 224
 //GO(__NR_gettid, ".s .m");
 //SY(__NR_gettid);

   // __NR_readahead 225
 //GO(__NR_readahead, ".s .m");
 //SY(__NR_readahead);

   // __NR_setxattr 226
 //GO(__NR_setxattr, ".s .m");
 //SY(__NR_setxattr);

   // __NR_lsetxattr 227
 //GO(__NR_lsetxattr, ".s .m");
 //SY(__NR_lsetxattr);

   // __NR_fsetxattr 228
 //GO(__NR_fsetxattr, ".s .m");
 //SY(__NR_fsetxattr);

   // __NR_getxattr 229
 //GO(__NR_getxattr, ".s .m");
 //SY(__NR_getxattr);

   // __NR_lgetxattr 230
 //GO(__NR_lgetxattr, ".s .m");
 //SY(__NR_lgetxattr);

   // __NR_fgetxattr 231
 //GO(__NR_fgetxattr, ".s .m");
 //SY(__NR_fgetxattr);

   // __NR_listxattr 232
 //GO(__NR_listxattr, ".s .m");
 //SY(__NR_listxattr);

   // __NR_llistxattr 233
 //GO(__NR_llistxattr, ".s .m");
 //SY(__NR_llistxattr);

   // __NR_flistxattr 234
 //GO(__NR_flistxattr, ".s .m");
 //SY(__NR_flistxattr);

   // __NR_removexattr 235
 //GO(__NR_removexattr, ".s .m");
 //SY(__NR_removexattr);

   // __NR_lremovexattr 236
 //GO(__NR_lremovexattr, ".s .m");
 //SY(__NR_lremovexattr);

   // __NR_fremovexattr 237
 //GO(__NR_fremovexattr, ".s .m");
 //SY(__NR_fremovexattr);

   // __NR_tkill 238
 //GO(__NR_tkill, ".s .m");
 //SY(__NR_tkill);

   // __NR_sendfile64 239
 //GO(__NR_sendfile64, ".s .m");
 //SY(__NR_sendfile64);

   // __NR_futex 240
 //GO(__NR_futex, ".s .m");
 //SY(__NR_futex);

   // __NR_sched_setaffinity 241
 //GO(__NR_sched_setaffinity, ".s .m");
 //SY(__NR_sched_setaffinity);

   // __NR_sched_getaffinity 242
 //GO(__NR_sched_getaffinity, ".s .m");
 //SY(__NR_sched_getaffinity);

   // __NR_set_thread_area 243
 //GO(__NR_set_thread_area, ".s .m");
 //SY(__NR_set_thread_area);

   // __NR_get_thread_area 244
 //GO(__NR_get_thread_area, ".s .m");
 //SY(__NR_get_thread_area);

   // __NR_io_setup 245
 //GO(__NR_io_setup, ".s .m");
 //SY(__NR_io_setup);

   // __NR_io_destroy 246
 //GO(__NR_io_destroy, ".s .m");
 //SY(__NR_io_destroy);

   // __NR_io_getevents 247
 //GO(__NR_io_getevents, ".s .m");
 //SY(__NR_io_getevents);

   // __NR_io_submit 248
 //GO(__NR_io_submit, ".s .m");
 //SY(__NR_io_submit);

   // __NR_io_cancel 249
 //GO(__NR_io_cancel, ".s .m");
 //SY(__NR_io_cancel);

   // __NR_fadvise64 250
 //GO(__NR_fadvise64, ".s .m");
 //SY(__NR_fadvise64);

   // 251 --> sys_ni_syscall()
   GO(251, "0e");
   SY(251);

   // __NR_exit_group 252

   // __NR_lookup_dcookie 253 --> sys_lookup_dcookie()
   GO(__NR_lookup_dcookie, "4s 1m");
   SY(__NR_lookup_dcookie, x0, x0, x0, x0+1);

   // __NR_epoll_create 254
 //GO(__NR_epoll_create, ".s .m");
 //SY(__NR_epoll_create);

   // __NR_epoll_ctl 255
 //GO(__NR_epoll_ctl, ".s .m");
 //SY(__NR_epoll_ctl);

   // __NR_epoll_wait 256
 //GO(__NR_epoll_wait, ".s .m");
 //SY(__NR_epoll_wait);

   // __NR_remap_file_pages 257
 //GO(__NR_remap_file_pages, ".s .m");
 //SY(__NR_remap_file_pages);

   // __NR_set_tid_address 258
 //GO(__NR_set_tid_address, ".s .m");
 //SY(__NR_set_tid_address);

   // __NR_timer_create 259
 //GO(__NR_timer_create, ".s .m");
 //SY(__NR_timer_create);

   // __NR_timer_settime (__NR_timer_create+1)
 //GO(__NR_timer_settime, ".s .m");
 //SY(__NR_timer_settime);

   // __NR_timer_gettime (__NR_timer_create+2)
 //GO(__NR_timer_gettime, ".s .m");
 //SY(__NR_timer_gettime);

   // __NR_timer_getoverrun (__NR_timer_create+3)
 //GO(__NR_timer_getoverrun, ".s .m");
 //SY(__NR_timer_getoverrun);

   // __NR_timer_delete (__NR_timer_create+4)
 //GO(__NR_timer_delete, ".s .m");
 //SY(__NR_timer_delete);

   // __NR_clock_settime (__NR_timer_create+5)
 //GO(__NR_clock_settime, ".s .m");
 //SY(__NR_clock_settime);

   // __NR_clock_gettime (__NR_timer_create+6)
 //GO(__NR_clock_gettime, ".s .m");
 //SY(__NR_clock_gettime);

   // __NR_clock_getres (__NR_timer_create+7)
 //GO(__NR_clock_getres, ".s .m");
 //SY(__NR_clock_getres);

   // __NR_clock_nanosleep (__NR_timer_create+8)
 //GO(__NR_clock_nanosleep, ".s .m");
 //SY(__NR_clock_nanosleep);

   // __NR_statfs64 268
 //GO(__NR_statfs64, ".s .m");
 //SY(__NR_statfs64);

   // __NR_fstatfs64 269
 //GO(__NR_fstatfs64, ".s .m");
 //SY(__NR_fstatfs64);

   // __NR_tgkill 270
 //GO(__NR_tgkill, ".s .m");
 //SY(__NR_tgkill);

   // __NR_utimes 271
 //GO(__NR_utimes, ".s .m");
 //SY(__NR_utimes);

   // __NR_fadvise64_64 272
 //GO(__NR_fadvise64_64, ".s .m");
 //SY(__NR_fadvise64_64);

   // __NR_vserver 273 --> sys_ni_syscall()
 //GO(__NR_vserver, "0e");
 //SY(__NR_vserver);

   // __NR_mbind 274
 //GO(__NR_mbind, ".s .m");
 //SY(__NR_mbind);

   // __NR_get_mempolicy 275
 //GO(__NR_get_mempolicy, ".s .m");
 //SY(__NR_get_mempolicy);

   // __NR_set_mempolicy 276
 //GO(__NR_set_mempolicy, ".s .m");
 //SY(__NR_set_mempolicy);

   // __NR_mq_open  277
 //GO(__NR_mq_open, ".s .m");
 //SY(__NR_mq_open);

   // __NR_mq_unlink (__NR_mq_open+1)
 //GO(__NR_mq_unlink, ".s .m");
 //SY(__NR_mq_unlink);

   // __NR_mq_timedsend (__NR_mq_open+2)
 //GO(__NR_mq_timedsend, ".s .m");
 //SY(__NR_mq_timedsend);

   // __NR_mq_timedreceive (__NR_mq_open+3)
 //GO(__NR_mq_timedreceive, ".s .m");
 //SY(__NR_mq_timedreceive);

   // __NR_mq_notify (__NR_mq_open+4)
 //GO(__NR_mq_notify, ".s .m");
 //SY(__NR_mq_notify);

   // __NR_mq_getsetattr (__NR_mq_open+5)
 //GO(__NR_mq_getsetattr, ".s .m");
 //SY(__NR_mq_getsetattr);
   
   // __NR_sys_kexec_load 283 --> sys_ni_syscall()
 //GO(__NR_sys_kexec_load, "0e");
 //SY(__NR_sys_kexec_load);

   GO(9999, "1e");
   SY(9999);

   // __NR_exit 1 --> sys_exit()
   GO(__NR_exit, "1s 0m");
   SY(__NR_exit, x0);

   assert(0);
}


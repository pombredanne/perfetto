/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/syscall_tracker.h"

#include <utility>

#include <inttypes.h>

#include "src/trace_processor/slice_tracker.h"
#include "src/trace_processor/stats.h"

namespace perfetto {
namespace trace_processor {
namespace {

// Syscall number to string.
// https://thog.github.io/syscalls-table-aarch64/latest.html
constexpr std::array<const char*, kSyscallCount> aarch64_to_syscall = {{
    "restart_syscall",         //
    "exit",                    //
    "",                        //
    "sys_read",                //
    "sys_write",               //
    "open",                    //
    "close",                   //
    "",                        //
    "creat",                   //
    "link",                    //
    "unlink",                  //
    "execve",                  //
    "chdir",                   //
    "",                        //
    "mknod",                   //
    "chmod",                   //
    "",                        //
    "",                        //
    "",                        //
    "lseek",                   //
    "getpid",                  //
    "mount",                   //
    "umount",                  //
    "",                        //
    "",                        //
    "",                        //
    "ptrace",                  //
    "",                        //
    "",                        //
    "",                        //
    "",                        //
    "",                        //
    "",                        //
    "access",                  //
    "",                        //
    "",                        //
    "sync",                    //
    "kill",                    //
    "rename",                  //
    "mkdir",                   //
    "rmdir",                   //
    "dup",                     //
    "pipe",                    //
    "times",                   //
    "",                        //
    "brk",                     //
    "",                        //
    "",                        //
    "",                        //
    "",                        //
    "",                        //
    "acct",                    //
    "umount2",                 //
    "",                        //
    "ioctl",                   //
    "fcntl",                   //
    "",                        //
    "setpgid",                 //
    "",                        //
    "",                        //
    "umask",                   //
    "chroot",                  //
    "ustat",                   //
    "dup2",                    //
    "getppid",                 //
    "",                        //
    "setsid",                  //
    "",                        //
    "",                        //
    "",                        //
    "",                        //
    "",                        //
    "",                        //
    "",                        //
    "sethostname",             //
    "setrlimit",               //
    "",                        //
    "getrusage",               //
    "gettimeofday",            //
    "settimeofday",            //
    "",                        //
    "",                        //
    "",                        //
    "symlink",                 //
    "",                        //
    "readlink",                //
    "uselib",                  //
    "swapon",                  //
    "reboot",                  //
    "",                        //
    "",                        //
    "munmap",                  //
    "truncate",                //
    "ftruncate",               //
    "fchmod",                  //
    "",                        //
    "getpriority",             //
    "setpriority",             //
    "",                        //
    "statfs",                  //
    "fstatfs",                 //
    "",                        //
    "",                        //
    "syslog",                  //
    "setitimer",               //
    "getitimer",               //
    "stat",                    //
    "lstat",                   //
    "fstat",                   //
    "",                        //
    "",                        //
    "vhangup",                 //
    "",                        //
    "",                        //
    "wait4",                   //
    "swapoff",                 //
    "sysinfo",                 //
    "",                        //
    "fsync",                   //
    "",                        //
    "clone",                   //
    "setdomainname",           //
    "uname",                   //
    "",                        //
    "adjtimex",                //
    "mprotect",                //
    "",                        //
    "",                        //
    "init_module",             //
    "delete_module",           //
    "",                        //
    "quotactl",                //
    "getpgid",                 //
    "fchdir",                  //
    "bdflush",                 //
    "",                        //
    "personality",             //
    "",                        //
    "",                        //
    "",                        //
    "_llseek",                 //
    "",                        //
    "",                        //
    "flock",                   //
    "msync",                   //
    "readv",                   //
    "writev",                  //
    "getsid",                  //
    "fdatasync",               //
    "_sysctl",                 //
    "mlock",                   //
    "munlock",                 //
    "mlockall",                //
    "munlockall",              //
    "sched_setparam",          //
    "sched_getparam",          //
    "sched_setscheduler",      //
    "sched_getscheduler",      //
    "sched_yield",             //
    "sched_get_priority_max",  //
    "sched_get_priority_min",  //
    "",                        //
    "nanosleep",               //
    "mremap",                  //
    "",                        //
    "",                        //
    "",                        //
    "",                        //
    "poll",                    //
    "",                        //
    "",                        //
    "",                        //
    "prctl",                   //
    "",                        //
    "rt_sigaction",            //
    "rt_sigprocmask",          //
    "rt_sigpending",           //
    "",                        //
    "",                        //
    "rt_sigsuspend",           //
    "pread64",                 //
    "pwrite64",                //
    "",                        //
    "getcwd",                  //
    "capget",                  //
    "capset",                  //
    "sigaltstack",             //
    "sendfile",                //
    "",                        //
    "",                        //
    "vfork",                   //
    "ugetrlimit",              //
    "",                        //
    "",                        //
    "",                        //
    "stat64",                  //
    "lstat64",                 //
    "fstat64",                 //
    "lchown32",                //
    "getuid32",                //
    "getgid32",                //
    "geteuid32",               //
    "getegid32",               //
    "setreuid32",              //
    "setregid32",              //
    "getgroups32",             //
    "setgroups32",             //
    "fchown32",                //
    "setresuid32",             //
    "getresuid32",             //
    "setresgid32",             //
    "getresgid32",             //
    "chown32",                 //
    "setuid32",                //
    "setgid32",                //
    "setfsuid32",              //
    "setfsgid32",              //
    "getdents64",              //
    "pivot_root",              //
    "mincore",                 //
    "madvise",                 //
    "",                        //
    "",                        //
    "",                        //
    "gettid",                  //
    "readahead",               //
    "setxattr",                //
    "lsetxattr",               //
    "fsetxattr",               //
    "getxattr",                //
    "lgetxattr",               //
    "fgetxattr",               //
    "listxattr",               //
    "llistxattr",              //
    "flistxattr",              //
    "removexattr",             //
    "lremovexattr",            //
    "fremovexattr",            //
    "tkill",                   //
    "sendfile64",              //
    "futex",                   //
    "",                        //
    "",                        //
    "io_setup",                //
    "io_destroy",              //
    "io_getevents",            //
    "io_submit",               //
    "io_cancel",               //
    "exit_group",              //
    "lookup_dcookie",          //
    "epoll_create",            //
    "epoll_ctl",               //
    "epoll_wait",              //
    "remap_file_pages",        //
    "",                        //
    "",                        //
    "set_tid_address",         //
    "timer_create",            //
    "timer_settime",           //
    "timer_gettime",           //
    "timer_getoverrun",        //
    "timer_delete",            //
    "clock_settime",           //
    "clock_gettime",           //
    "clock_getres",            //
    "",                        //
    "",                        //
    "",                        //
    "tgkill",                  //
    "utimes",                  //
    "",                        //
    "",                        //
    "",                        //
    "",                        //
    "mq_open",                 //
    "mq_unlink",               //
    "mq_timedsend",            //
    "",                        //
    "mq_notify",               //
    "mq_getsetattr",           //
    "waitid",                  //
    "socket",                  //
    "bind",                    //
    "connect",                 //
    "listen",                  //
    "accept",                  //
    "getsockname",             //
    "getpeername",             //
    "socketpair",              //
    "send",                    //
    "sendto",                  //
    "recv",                    //
    "recvfrom",                //
    "shutdown",                //
    "setsockopt",              //
    "getsockopt",              //
    "sendmsg",                 //
    "recvmsg",                 //
    "semop",                   //
    "semget",                  //
    "semctl",                  //
    "msgsnd",                  //
    "msgrcv",                  //
    "msgget",                  //
    "msgctl",                  //
    "shmat",                   //
    "shmdt",                   //
    "shmget",                  //
    "shmctl",                  //
    "add_key",                 //
    "request_key",             //
    "keyctl",                  //
    "semtimedop",              //
    "",                        //
    "ioprio_set",              //
    "ioprio_get",              //
    "inotify_init",            //
    "inotify_add_watch",       //
    "inotify_rm_watch",        //
    "mbind",                   //
    "get_mempolicy",           //
    "set_mempolicy",           //
    "openat",                  //
    "mkdirat",                 //
    "mknodat",                 //
    "fchownat",                //
    "futimesat",               //
    "fstatat64",               //
    "unlinkat",                //
    "renameat",                //
    "linkat",                  //
    "symlinkat",               //
    "readlinkat",              //
    "fchmodat",                //
    "faccessat",               //
    "pselect6",                //
    "ppoll",                   //
    "unshare",                 //
    "",                        //
    "",                        //
    "splice",                  //
    "",                        //
    "tee",                     //
    "vmsplice",                //
    "move_pages",              //
    "getcpu",                  //
    "epoll_pwait",             //
    "kexec_load",              //
    "utimensat",               //
    "signalfd",                //
    "timerfd_create",          //
    "eventfd",                 //
    "fallocate",               //
    "",                        //
    "",                        //
    "signalfd4",               //
    "eventfd2",                //
    "epoll_create1",           //
    "dup3",                    //
    "pipe2",                   //
    "inotify_init1",           //
    "preadv",                  //
    "pwritev",                 //
    "",                        //
    "perf_event_open",         //
    "recvmmsg",                //
    "accept4",                 //
    "fanotify_init",           //
    "fanotify_mark",           //
    "prlimit64",               //
    "name_to_handle_at",       //
    "",                        //
    "clock_adjtime",           //
    "syncfs",                  //
    "sendmmsg",                //
    "setns",                   //
    "",                        //
    "",                        //
    "kcmp",                    //
    "finit_module",            //
    "sched_setattr",           //
    "sched_getattr",           //
    "renameat2",               //
    "seccomp",                 //
    "getrandom",               //
    "memfd_create",            //
    "bpf",                     //
    "execveat",                //
    "userfaultfd",             //
    "membarrier",              //
    "mlock2",                  //
    "copy_file_range",         //
    "preadv2",                 //
    "pwritev2",                //
    "pkey_mprotect",           //
    "pkey_alloc",              //
    "pkey_free",               //
    "statx",                   //
}};

// Syscall number to string.
// https://filippo.io/linux-syscall-table/
// http://blog.rchapman.org/posts/Linux_System_Call_Table_for_x86_64/
constexpr std::array<const char*, kSyscallCount> x86_64_to_syscall = {{
    "sys_read",                    //
    "sys_write",                   //
    "sys_open",                    //
    "sys_close",                   //
    "sys_newstat",                 //
    "sys_newfstat",                //
    "sys_newlstat",                //
    "sys_poll",                    //
    "sys_lseek",                   //
    "sys_mmap",                    //
    "sys_mprotect",                //
    "sys_munmap",                  //
    "sys_brk",                     //
    "sys_rt_sigaction",            //
    "sys_rt_sigprocmask",          //
    "stub_rt_sigreturn",           //
    "sys_ioctl",                   //
    "sys_pread64",                 //
    "sys_pwrite64",                //
    "sys_readv",                   //
    "sys_writev",                  //
    "sys_access",                  //
    "sys_pipe",                    //
    "sys_select",                  //
    "sys_sched_yield",             //
    "sys_mremap",                  //
    "sys_msync",                   //
    "sys_mincore",                 //
    "sys_madvise",                 //
    "sys_shmget",                  //
    "sys_shmat",                   //
    "sys_shmctl",                  //
    "sys_dup",                     //
    "sys_dup2",                    //
    "sys_pause",                   //
    "sys_nanosleep",               //
    "sys_getitimer",               //
    "sys_alarm",                   //
    "sys_setitimer",               //
    "sys_getpid",                  //
    "sys_sendfile64",              //
    "sys_socket",                  //
    "sys_connect",                 //
    "sys_accept",                  //
    "sys_sendto",                  //
    "sys_recvfrom",                //
    "sys_sendmsg",                 //
    "sys_recvmsg",                 //
    "sys_shutdown",                //
    "sys_bind",                    //
    "sys_listen",                  //
    "sys_getsockname",             //
    "sys_getpeername",             //
    "sys_socketpair",              //
    "sys_setsockopt",              //
    "sys_getsockopt",              //
    "stub_clone",                  //
    "stub_fork",                   //
    "stub_vfork",                  //
    "stub_execve",                 //
    "sys_exit",                    //
    "sys_wait4",                   //
    "sys_kill",                    //
    "sys_newuname",                //
    "sys_semget",                  //
    "sys_semop",                   //
    "sys_semctl",                  //
    "sys_shmdt",                   //
    "sys_msgget",                  //
    "sys_msgsnd",                  //
    "sys_msgrcv",                  //
    "sys_msgctl",                  //
    "sys_fcntl",                   //
    "sys_flock",                   //
    "sys_fsync",                   //
    "sys_fdatasync",               //
    "sys_truncate",                //
    "sys_ftruncate",               //
    "sys_getdents",                //
    "sys_getcwd",                  //
    "sys_chdir",                   //
    "sys_fchdir",                  //
    "sys_rename",                  //
    "sys_mkdir",                   //
    "sys_rmdir",                   //
    "sys_creat",                   //
    "sys_link",                    //
    "sys_unlink",                  //
    "sys_symlink",                 //
    "sys_readlink",                //
    "sys_chmod",                   //
    "sys_fchmod",                  //
    "sys_chown",                   //
    "sys_fchown",                  //
    "sys_lchown",                  //
    "sys_umask",                   //
    "sys_gettimeofday",            //
    "sys_getrlimit",               //
    "sys_getrusage",               //
    "sys_sysinfo",                 //
    "sys_times",                   //
    "sys_ptrace",                  //
    "sys_getuid",                  //
    "sys_syslog",                  //
    "sys_getgid",                  //
    "sys_setuid",                  //
    "sys_setgid",                  //
    "sys_geteuid",                 //
    "sys_getegid",                 //
    "sys_setpgid",                 //
    "sys_getppid",                 //
    "sys_getpgrp",                 //
    "sys_setsid",                  //
    "sys_setreuid",                //
    "sys_setregid",                //
    "sys_getgroups",               //
    "sys_setgroups",               //
    "sys_setresuid",               //
    "sys_getresuid",               //
    "sys_setresgid",               //
    "sys_getresgid",               //
    "sys_getpgid",                 //
    "sys_setfsuid",                //
    "sys_setfsgid",                //
    "sys_getsid",                  //
    "sys_capget",                  //
    "sys_capset",                  //
    "sys_rt_sigpending",           //
    "sys_rt_sigtimedwait",         //
    "sys_rt_sigqueueinfo",         //
    "sys_rt_sigsuspend",           //
    "sys_sigaltstack",             //
    "sys_utime",                   //
    "sys_mknod",                   //
    "",                            // uselib
    "sys_personality",             //
    "sys_ustat",                   //
    "sys_statfs",                  //
    "sys_fstatfs",                 //
    "sys_sysfs",                   //
    "sys_getpriority",             //
    "sys_setpriority",             //
    "sys_sched_setparam",          //
    "sys_sched_getparam",          //
    "sys_sched_setscheduler",      //
    "sys_sched_getscheduler",      //
    "sys_sched_get_priority_max",  //
    "sys_sched_get_priority_min",  //
    "sys_sched_rr_get_interval",   //
    "sys_mlock",                   //
    "sys_munlock",                 //
    "sys_mlockall",                //
    "sys_munlockall",              //
    "sys_vhangup",                 //
    "sys_modify_ldt",              //
    "sys_pivot_root",              //
    "sys_sysctl",                  //
    "sys_prctl",                   //
    "sys_arch_prctl",              //
    "sys_adjtimex",                //
    "sys_setrlimit",               //
    "sys_chroot",                  //
    "sys_sync",                    //
    "sys_acct",                    //
    "sys_settimeofday",            //
    "sys_mount",                   //
    "sys_umount",                  //
    "sys_swapon",                  //
    "sys_swapoff",                 //
    "sys_reboot",                  //
    "sys_sethostname",             //
    "sys_setdomainname",           //
    "stub_iopl",                   //
    "sys_ioperm",                  //
    "",                            // create_module
    "sys_init_module",             //
    "sys_delete_module",           //
    "",                            // get_kernel_syms
    "",                            // query_module
    "sys_quotactl",                //
    "",                            // nfsservctl
    "",                            // getpmsg
    "",                            // putpmsg
    "",                            // afs_syscall
    "",                            // tuxcall
    "",                            // security
    "sys_gettid",                  //
    "sys_readahead",               //
    "sys_setxattr",                //
    "sys_lsetxattr",               //
    "sys_fsetxattr",               //
    "sys_getxattr",                //
    "sys_lgetxattr",               //
    "sys_fgetxattr",               //
    "sys_listxattr",               //
    "sys_llistxattr",              //
    "sys_flistxattr",              //
    "sys_removexattr",             //
    "sys_lremovexattr",            //
    "sys_fremovexattr",            //
    "sys_tkill",                   //
    "sys_time",                    //
    "sys_futex",                   //
    "sys_sched_setaffinity",       //
    "sys_sched_getaffinity",       //
    "",                            // set_thread_area
    "sys_io_setup",                //
    "sys_io_destroy",              //
    "sys_io_getevents",            //
    "sys_io_submit",               //
    "sys_io_cancel",               //
    "",                            // get_thread_area
    "sys_lookup_dcookie",          //
    "sys_epoll_create",            //
    "",                            // epoll_ctl_old
    "",                            // epoll_wait_old
    "sys_remap_file_pages",        //
    "sys_getdents64",              //
    "sys_set_tid_address",         //
    "sys_restart_syscall",         //
    "sys_semtimedop",              //
    "sys_fadvise64",               //
    "sys_timer_create",            //
    "sys_timer_settime",           //
    "sys_timer_gettime",           //
    "sys_timer_getoverrun",        //
    "sys_timer_delete",            //
    "sys_clock_settime",           //
    "sys_clock_gettime",           //
    "sys_clock_getres",            //
    "sys_clock_nanosleep",         //
    "sys_exit_group",              //
    "sys_epoll_wait",              //
    "sys_epoll_ctl",               //
    "sys_tgkill",                  //
    "sys_utimes",                  //
    "",                            // vserver
    "sys_mbind",                   //
    "sys_set_mempolicy",           //
    "sys_get_mempolicy",           //
    "sys_mq_open",                 //
    "sys_mq_unlink",               //
    "sys_mq_timedsend",            //
    "sys_mq_timedreceive",         //
    "sys_mq_notify",               //
    "sys_mq_getsetattr",           //
    "sys_kexec_load",              //
    "sys_waitid",                  //
    "sys_add_key",                 //
    "sys_request_key",             //
    "sys_keyctl",                  //
    "sys_ioprio_set",              //
    "sys_ioprio_get",              //
    "sys_inotify_init",            //
    "sys_inotify_add_watch",       //
    "sys_inotify_rm_watch",        //
    "sys_migrate_pages",           //
    "sys_openat",                  //
    "sys_mkdirat",                 //
    "sys_mknodat",                 //
    "sys_fchownat",                //
    "sys_futimesat",               //
    "sys_newfstatat",              //
    "sys_unlinkat",                //
    "sys_renameat",                //
    "sys_linkat",                  //
    "sys_symlinkat",               //
    "sys_readlinkat",              //
    "sys_fchmodat",                //
    "sys_faccessat",               //
    "sys_pselect6",                //
    "sys_ppoll",                   //
    "sys_unshare",                 //
    "sys_set_robust_list",         //
    "sys_get_robust_list",         //
    "sys_splice",                  //
    "sys_tee",                     //
    "sys_sync_file_range",         //
    "sys_vmsplice",                //
    "sys_move_pages",              //
    "sys_utimensat",               //
    "sys_epoll_pwait",             //
    "sys_signalfd",                //
    "sys_timerfd_create",          //
    "sys_eventfd",                 //
    "sys_fallocate",               //
    "sys_timerfd_settime",         //
    "sys_timerfd_gettime",         //
    "sys_accept4",                 //
    "sys_signalfd4",               //
    "sys_eventfd2",                //
    "sys_epoll_create1",           //
    "sys_dup3",                    //
    "sys_pipe2",                   //
    "sys_inotify_init1",           //
    "sys_preadv",                  //
    "sys_pwritev",                 //
    "sys_rt_tgsigqueueinfo",       //
    "sys_perf_event_open",         //
    "sys_recvmmsg",                //
    "sys_fanotify_init",           //
    "sys_fanotify_mark",           //
    "sys_prlimit64",               //
    "sys_name_to_handle_at",       //
    "sys_open_by_handle_at",       //
    "sys_clock_adjtime",           //
    "sys_syncfs",                  //
    "sys_sendmmsg",                //
    "sys_setns",                   //
    "sys_getcpu",                  //
    "sys_process_vm_readv",        //
    "sys_process_vm_writev",       //
    "sys_kcmp",                    //
    "sys_finit_module",            //
}};

// When we don't know the architecture map every syscall number to
// null string.
constexpr std::array<const char*, kSyscallCount> unknown_to_syscall = {{}};

}  // namespace

SyscallTracker::SyscallTracker(TraceProcessorContext* context)
    : context_(context) {
  // This sets arch_syscall_to_string_id_
  SetArchitecture(kUnknown);
}

SyscallTracker::~SyscallTracker() = default;

void SyscallTracker::SetArchitecture(Architecture arch) {
  const std::array<const char*, kSyscallCount>* arch_to_generic_syscall_number;
  switch (arch) {
    case kAarch64:
      arch_to_generic_syscall_number = &aarch64_to_syscall;
      break;
    case kX86_64:
      arch_to_generic_syscall_number = &x86_64_to_syscall;
      break;
    case kUnknown:
      arch_to_generic_syscall_number = &unknown_to_syscall;
      break;
  }

  for (size_t i = 0; i < kSyscallCount; i++) {
    const char* name = (*arch_to_generic_syscall_number)[i];
    StringId id = context_->storage->InternString(name ? name : "");
    arch_syscall_to_string_id_[i] = id;
    if (name && !strcmp(name, "sys_write"))
      sys_write_string_id_ = id;
  }
}

void SyscallTracker::Enter(int64_t ts, UniqueTid utid, uint32_t syscall_num) {
  StringId name = SyscallNumberToStringId(syscall_num);
  if (!name) {
    context_->storage->IncrementStats(stats::sys_unknown_syscall);
    return;
  }

  context_->slice_tracker->Begin(ts, utid, 0 /* cat */, name);
}

void SyscallTracker::Exit(int64_t ts, UniqueTid utid, uint32_t syscall_num) {
  StringId name = SyscallNumberToStringId(syscall_num);
  if (!name) {
    context_->storage->IncrementStats(stats::sys_unknown_syscall);
    return;
  }

  context_->slice_tracker->End(ts, utid, 0 /* cat */, name);
}

StringId SyscallTracker::SyscallNumberToStringId(uint32_t syscall_num) {
  if (syscall_num > kSyscallCount)
    return 0;
  // We see two write sys calls around each userspace slice that is going via
  // trace_marker, this violates the assumption that userspace slices are
  // perfectly nested. For the moment ignore all write sys calls.
  // TODO(hjd): Remove this limitation.
  StringId id = arch_syscall_to_string_id_[syscall_num];
  if (id == sys_write_string_id_)
    return 0;
  return id;
}

}  // namespace trace_processor
}  // namespace perfetto

#include <jdebug.h>

#include "../common/stb_sprintf.h"
#include "jdebug_priv.h"
#include "syscall.h"

#include <immintrin.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/ucontext.h>
#include <sys/wait.h>
#include <unistd.h>


static char g_log_filename[FILENAME_MAX]  = {0};
static char g_core_filename[FILENAME_MAX] = {0};
static _Atomic int g_init_count           = 0;
static size_t g_stack_size                = 8 * 1024 * 1024;

#define JDEBUG_CHILD_ARG "_jdc"

static void jdebug_parse_args_and_run_child(int argc, char* argv[]) __attribute__((noreturn));
static void jdebug_demangle_helper(int argc, char* argv[]) __attribute__((noreturn));

void jdebug_crash_handler_early(int argc, char* argv[])
{
  for (int i = 0; i < argc; ++i) {
    if (0 == strcmp(argv[i], JDEBUG_CHILD_ARG))
      jdebug_parse_args_and_run_child(argc, argv);

    if (0 == strcmp(argv[i], JDEBUG_DEMANGLE_ARG))
      jdebug_demangle_helper(argc - i, argv + i);
  }
}

static int jdebug_make_path_absolute(const char* path, char* out)
{
  if (path[0] == '/') {
    strncpy(out, path, FILENAME_MAX);
    return 0;
  }

  char cwd[FILENAME_MAX] = {0};
  if (!getcwd(cwd, FILENAME_MAX))
    return -1;

  if (snprintf(out, FILENAME_MAX, "%s/%s", cwd, path) < (int) (strlen(cwd) + strlen(path) + 1))
    return -1;

  return 0;
}

void jdebug_crash_handler_init(jdebug_crash_handler_init_info_t* init_info)
{
  if (atomic_fetch_add(&g_init_count, 1) == 0) {
    if (NULL != init_info->log_filename) {
      jdebug_make_path_absolute(init_info->log_filename, g_log_filename);
    } else {
      memset(g_log_filename, 0, FILENAME_MAX);
    }

    if (NULL != init_info->coredump_filename) {
      jdebug_make_path_absolute(init_info->coredump_filename, g_core_filename);
    } else {
      memset(g_core_filename, 0, FILENAME_MAX);
    }

    struct rlimit stack_limit;
    if (!getrlimit(RLIMIT_STACK, &stack_limit)) {
      g_stack_size = stack_limit.rlim_cur;
    }

    jdebug_install_signal_handlers();
  }
}

void jdebug_crash_handler_deinit()
{
  if (atomic_fetch_sub(&g_init_count, 1) == 1) {
    jdebug_uninstall_signal_handlers();
    memset(g_log_filename, 0, sizeof(g_log_filename));
    memset(g_core_filename, 0, sizeof(g_core_filename));
  }
}

struct exec_data
{
  pid_t crash_pid;
  pid_t crash_tid;
  char* executable;
  char* log_filename;
  char* coredump_filename;
  int stdin_fds[2];
};

static int jdebug_exec_dump_child(struct exec_data* data);

// runs inside a signal handler
// must be async-signal-safe
// see `man 7 signal-safety`
void jdebug_crash_handler(siginfo_t* info, void* ucontext)
{
  logger_t log;
  jdebug_log_init(&log, g_log_filename);

  jdebug_print_signal_info(&log, info, ucontext);

  char* stack_base  = NULL;
  pid_t child_pid   = -1;
  bool child_exited = false;

  long err;

  const int dumpable_status = SYSCALL(SYS_prctl, PR_GET_DUMPABLE);
  if (1 != dumpable_status) {
    if ((err = SYSCALL(SYS_prctl, PR_SET_DUMPABLE, 1)) < 0) {
      jdebug_log_format(&log, "error occurred handling crash\n");
      jdebug_log_format(&log, "failed to set process as dumpable: %ld\n", -err);
      goto done;
    }
  }

  struct exec_data data;
  data.crash_pid         = SYSCALL(SYS_getpid);
  data.crash_tid         = SYSCALL(SYS_gettid);
  data.log_filename      = g_log_filename[0] ? g_log_filename : NULL;
  data.coredump_filename = g_core_filename[0] ? g_core_filename : NULL;

  char self_buf[FILENAME_MAX] = {0};
  if ((err = SYSCALL(SYS_readlink, "/proc/self/exe", self_buf, sizeof(self_buf))) < 0) {
    jdebug_log_format(&log, "error occurred handling crash\n");
    jdebug_log_format(&log, "failed to read /proc/self/exe: %ld\n", -err);
    goto done;
  }
  self_buf[err]   = '\0';
  data.executable = self_buf;

  SYSCALL(SYS_pipe, data.stdin_fds);

  err = SYSCALL(SYS_mmap, NULL, g_stack_size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  if (err < 0) {
    jdebug_log_format(&log, "error occurred handling crash\n");
    jdebug_log_format(&log, "failed to allocate stack: %ld\n", -err);
    goto done;
  }

  _mm_sfence();

  stack_base      = (void*) err;
  void* stack_top = stack_base + g_stack_size;

  err = SYSCLONE(jdebug_exec_dump_child, stack_top, CLONE_UNTRACED | SIGCHLD, &data, 0, 0, 0);
  if (err < 0) {
    jdebug_log_format(&log, "error occurred handling crash\n");
    jdebug_log_format(&log, "failed to clone: %ld\n", -err);
    goto done;
  }

  child_pid = err;

  SYSCALL(SYS_close, data.stdin_fds[0]);
  const int stdin_fd = data.stdin_fds[1];

  // We know the child will block on reading the pipe.
  // If we don't have that block, there would be a race condition
  //  between PR_SET_PTRACER and PTRACE_ATTACH in the child,
  //  since we must start the child process to get its pid.
  // Alternatively, use PR_SET_PTRACER with PR_SET_PTRACER_ANY
  //  before clone.
  if ((err = SYSCALL(SYS_prctl, PR_SET_PTRACER, child_pid)) < 0) {
    jdebug_log_format(&log, "error occurred handling crash\n");
    jdebug_log_format(&log, "failed to set tracer: %ld\n", -err);
    goto done;
  }

  if ((err = jdebug_write_all(stdin_fd, info, sizeof(siginfo_t))) < 0) {
    jdebug_log_format(&log, "error occurred handling crash\n");
    jdebug_log_format(&log, "failed to write: %ld\n", -err);
    goto done;
  }

  if ((err = jdebug_write_all(stdin_fd, ucontext, sizeof(ucontext_t))) < 0) {
    jdebug_log_format(&log, "error occurred handling crash\n");
    jdebug_log_format(&log, "failed to write: %ld\n", -err);
    goto done;
  }

  if ((err = jdebug_collect_and_write_stacktrace(stdin_fd, ucontext))) {
    jdebug_log_format(&log, "error occurred handling crash\n");
    jdebug_log_format(&log, "failed to write: %ld\n", -err);
    goto done;
  }

  do {
    siginfo_t info;
    err = SYSCALL(SYS_waitid, P_PID, child_pid, &info, WEXITED);
    if (err < 0 && -err != EINTR) {
      break;
    }
    child_exited = info.si_code == CLD_EXITED
                   || info.si_code == CLD_KILLED
                   || info.si_code == CLD_DUMPED;
  } while (err < 0);

  if (err < 0) {
    jdebug_log_format(&log, "error occurred handling crash\n");
    jdebug_log_format(&log, "failed to wait on child: %ld\n", -err);
  }

done:
  if (child_pid > 0 && !child_exited)
    SYSCALL(SYS_kill, child_pid, SIGKILL);

  if (stack_base)
    SYSCALL(SYS_munmap, stack_base, g_stack_size);

  if (0 == dumpable_status)
    SYSCALL(SYS_prctl, PR_SET_DUMPABLE, 0);

  jdebug_log_deinit(&log);
}

static int jdebug_exec_dump_child(struct exec_data* data)
{
  const int INT_STR_SIZE = sizeof(int) * 8 + 1;
  char tid_str[INT_STR_SIZE];

  stbsp_snprintf(tid_str, INT_STR_SIZE, "%d", data->crash_tid);

  SYSCALL(SYS_dup2, data->stdin_fds[0], STDIN_FILENO);
  SYSCALL(SYS_close, data->stdin_fds[0]);
  SYSCALL(SYS_close, data->stdin_fds[1]);

  char* args[] = {
    data->executable,
    JDEBUG_CHILD_ARG,
    tid_str,
    data->coredump_filename ? data->coredump_filename : "",
    data->log_filename ? data->log_filename : "",
    NULL,
  };

  long err = SYSCALL(SYS_execve, data->executable, args, 0);

  logger_t log;
  jdebug_log_init(&log, data->log_filename);
  jdebug_log_format(&log, "error occurred handling crash\n");
  jdebug_log_format(&log, "failed to execv child: %ld\n", -err);
  jdebug_log_format(&log, "executable: %s\n", data->executable);
  jdebug_log_deinit(&log);

  return EXIT_FAILURE;
}

__attribute__((noreturn)) static void jdebug_parse_args_and_run_child(int argc, char* argv[])
{
  int i;
  for (i = 0; i < argc; ++i) {
    if (0 == strcmp(argv[i], JDEBUG_CHILD_ARG)) {
      break;
    }
  }

  if (i + 3 >= argc) {
    exit(EXIT_FAILURE);
  }

  const pid_t tid           = atoi(argv[++i]);
  const char* coredump_file = argv[++i];
  const char* log_file      = argv[++i];

  _Exit(jdebug_run_child(getppid(), tid, coredump_file, log_file));
}

__attribute__((noreturn)) static void jdebug_demangle_helper(int argc, char* argv[])
{
  if (argc < 2)
    _Exit(EXIT_FAILURE);

  if (0 != strcmp(argv[0], JDEBUG_DEMANGLE_ARG))
    _Exit(EXIT_FAILURE);

  char* demangled = jdebug_demangle(argv[1]);
  if (!demangled)
    _Exit(EXIT_FAILURE);

  const size_t demangled_len = strlen(demangled);
  const ssize_t write_result = jdebug_write_all(STDOUT_FILENO, demangled, demangled_len);

  if (write_result > 0 && (size_t) write_result == demangled_len)
    _Exit(EXIT_SUCCESS);

  _Exit(EXIT_FAILURE);
}

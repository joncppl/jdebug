#include "jdebug_priv.h"

#include "syscall.h"

#include <dlfcn.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sched.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>

extern char** environ;

long jdebug_gettime_monotonic_ms(void)
{
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time)) {
    return -1;
  }
  return time.tv_sec * 1000 + time.tv_nsec / (1000 * 1000);
}

ssize_t jdebug_read_all(int fd, void* buf, size_t size, long timeout_ms)
{
  long until, now;
  int available = 0;

  if (timeout_ms > 0) {
    if (-1 == ioctl(fd, FIONREAD, &available) || available < 0)
      return -1;

    now = jdebug_gettime_monotonic_ms();
    if (-1 == now)
      return -1;

    until = now + timeout_ms;

    if ((size_t) available < size) {
      struct epoll_event event;
      int epoll = epoll_create1(0);
      if (-1 == epoll)
        return -1;

      event.data.fd = fd;
      event.events  = EPOLLIN;

      if (-1 == epoll_ctl(epoll, EPOLL_CTL_ADD, fd, &event)) {
        close(epoll);
        return -1;
      }

      while ((size_t) available < size && now < until) {
        int r = epoll_wait(epoll, &event, 1, until - now);
        if (-1 == r) {
          close(epoll);
          return -1;
        }

        if (r > 0) {
          if (-1 == ioctl(fd, FIONREAD, &available) || available < 0) {
            close(epoll);
            return -1;
          }
        }

        now = jdebug_gettime_monotonic_ms();
        if (-1 == now) {
          close(epoll);
          return -1;
        }
      }

      close(epoll);
    }

    if (-1 == ioctl(fd, FIONREAD, &available) || available < 0)
      return -1;

    if ((size_t) available < size) {
      errno = EAGAIN;
      return -1;
    }
  }

  size_t total = 0;
  while (total < size) {
    const ssize_t r = read(fd, (char*) buf + total, size - total);
    if (-1 == r) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (0 == r)
      break;
    total += r;
  }

  return (ssize_t) total;
}

ssize_t jdebug_write_all(int fd, const void* buf, size_t size)
{
  size_t total = 0;
  while (total < size) {
    const ssize_t r = SYSCALL(SYS_write, fd, buf + total, size - total);
    if (r < 0) {
      if (-r == EINTR)
        continue;
      return r;
    }
    total += r;
  }
  return (ssize_t) total;
}

extern char* __cxa_demangle(const char*, char*, size_t*, int*) __attribute__((weak));

char* jdebug_demangle(const char* mangled)
{
  if (__cxa_demangle)
    return __cxa_demangle(mangled, NULL, NULL, NULL);
  return NULL;
}

typedef struct
{
  const char* mangled_symbol;
  int pipe;
} demangle_exec_data_t;

static int jdebug_demangled_exec(demangle_exec_data_t* data)
{
  SYSCALL(SYS_dup2, data->pipe, STDOUT_FILENO);
  SYSCALL(SYS_close, data->pipe);

  char self_buf[FILENAME_MAX] = {0};
  if (SYSCALL(SYS_readlink, "/proc/self/exe", self_buf, sizeof(self_buf) - 1) < 0)
    _Exit(EXIT_FAILURE);

  const char* args[] = {
    self_buf,
    JDEBUG_DEMANGLE_ARG,
    data->mangled_symbol,
    NULL,
  };

  SYSCALL(SYS_execve, self_buf, args, environ);
  return EXIT_FAILURE;
}

char* jdebug_demangle_safe(const char* mangled_symbol)
{
  int pipe[2]                    = {-1, -1};
  demangle_exec_data_t exec_data = {NULL, -1};
  pid_t child                    = -1;

  const intptr_t stack_size = 8 * 1024 * 1024;
  intptr_t stack_base       = 0;
  intptr_t stack_top        = 0;

  int demangled_len = 0;
  int exit_code     = 1;

  char* result = NULL;
  long err;

  err = SYSCALL(SYS_pipe, pipe);
  if (err < 0)
    goto done;

  err = SYSCALL(SYS_mmap, NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  if (err < 0)
    goto done;

  stack_base = err;
  stack_top  = stack_base + stack_size;

  exec_data.mangled_symbol = mangled_symbol;
  exec_data.pipe           = pipe[1];
  err                      = SYSCLONE(jdebug_demangled_exec, stack_top, CLONE_UNTRACED | SIGCHLD, &exec_data, 0, 0, 0);
  if (err < 0)
    goto done;

  child = (pid_t) err;

  SYSCALL(SYS_close, pipe[1]);
  pipe[1] = -1;

  do {
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    err = SYSCALL(SYS_waitid, P_PID, child, &info, WEXITED, NULL);
    if (err < 0) {
      if (-err != EINTR)
        break;
      continue;
    }

    exit_code = info.si_code == CLD_EXITED ? info.si_status : 1;
  } while (err < 0);

  if (exit_code)
    goto done;

  err = SYSCALL(SYS_ioctl, pipe[0], FIONREAD, &demangled_len);
  if (err < 0)
    goto done;

  err = SYSCALL(SYS_mmap, NULL, demangled_len + 1, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (err < 0)
    goto done;

  result = (char*) err;
  err    = jdebug_read_all(pipe[0], result, demangled_len, 0);
  if (err < 0) {
    SYSCALL(SYS_munmap, result, demangled_len + 1);
    result = NULL;
    goto done;
  }

  result[err] = 0;

done:
  if (pipe[0] >= 0)
    SYSCALL(SYS_close, pipe[0]);
  if (pipe[1] >= 0)
    SYSCALL(SYS_close, pipe[1]);

  if (stack_base)
    SYSCALL(SYS_munmap, stack_base, stack_size);

  return result;
}

void jdebug_demangle_safe_free(const char* demangled)
{
  if (demangled)
    SYSCALL(SYS_munmap, demangled, strlen(demangled) + 1);
}

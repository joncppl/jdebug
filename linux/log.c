#include "jdebug_priv.h"
#include "syscall.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_MIN 128
#include "../common/stb_sprintf.h"
#pragma GCC diagnostic pop

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int jdebug_log_init(logger_t* log, const char* filename)
{
  if ((NULL == filename || 0 == strlen(filename))) {
    log->fd = -1;
    memset(log->log_filename, 0, sizeof(log->log_filename));
    return 0;
  }

  log->fd = SYSCALL(SYS_open, filename, O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, 0644);
  if (log->fd < 0) {
    log->fd = -1;
    memset(log->log_filename, 0, sizeof(log->log_filename));
    return -1;
  }
  strncpy(log->log_filename, filename, sizeof(log->log_filename) - 1);
  log->log_filename[sizeof(log->log_filename) - 1] = 0;
  return 0;
}

void jdebug_log_deinit(logger_t* log)
{
  memset(log->log_filename, 0, sizeof(log->log_filename));
  if (-1 != log->fd) {
    SYSCALL(SYS_close, log->fd);
    log->fd = -1;
  }
}

static char* jdebug_printf_callback(const char* buf, void* user, int len)
{
  const logger_t* logger = user;
  if (logger->fd >= 0)
    SYSCALL(SYS_write, logger->fd, buf, len);
  SYSCALL(SYS_write, STDERR_FILENO, buf, len);
  return (char*) buf;
}

void jdebug_log_format(const logger_t* log, const char* fmt, ...)
{
  char buf[STB_SPRINTF_MIN];

  va_list args;
  va_start(args, fmt);

  stbsp_vsprintfcb(jdebug_printf_callback, (void*) log, buf, fmt, args);

  va_end(args);
}

#include "log.h"

#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_MIN 128
#include "../common/stb_sprintf.h"

#include <share.h>
#include <stdarg.h>
#include <string.h>

void jdebug_log_init(const wchar_t* log_filename, logger_t* logger)
{
  _flushall();

  if (log_filename) {

    logger->log_file = _wfsopen(log_filename, L"a", _SH_DENYNO);

    if (NULL == logger->log_file) {
      char error_buf[512] = {0};
      strerror_s(error_buf, sizeof(error_buf), errno);
      fprintf(stderr, "%s\n", error_buf);
    }

  } else {
    logger->log_file = NULL;
  }
}

static char* jdebug_printf_callback(const char* buf, void* user, int len)
{
  const logger_t* logger = user;
  if (logger->log_file)
    fwrite(buf, 1, len, logger->log_file);
  fwrite(buf, 1, len, stderr);
  return (char*) buf;
}

void jdebug_log_printf(const logger_t* logger, const char* fmt, ...)
{
  char buf[STB_SPRINTF_MIN];

  va_list args;
  va_start(args, fmt);

  stbsp_vsprintfcb(jdebug_printf_callback, (void*) logger, buf, fmt, args);

  va_end(args);
}

void jdebug_log_deinit(logger_t* logger)
{
  if (logger->log_file) {
    fflush(logger->log_file);
    fclose(logger->log_file);
    logger->log_file = NULL;
  }
}

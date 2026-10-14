#pragma once

#include <limits.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct
  {
    int fd;
    char log_filename[PATH_MAX + 1];
  } logger_t;

  int jdebug_log_init(logger_t* log, const char* filename);

  void jdebug_log_deinit(logger_t* log);

  void jdebug_log_format(const logger_t* log, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

#ifdef __cplusplus
}
#endif

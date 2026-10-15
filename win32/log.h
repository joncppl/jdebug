#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct
  {
    FILE* log_file;
  } logger_t;

  void jdebug_log_init(const wchar_t* log_filename, logger_t* logger);

#if defined(_MSC_VER)
#define JDEBUG_PRINTF_ARG _Printf_format_string_
#define JDEBUG_PRINTF_ATTR
#elif defined(__GNUC__)
#define JDEBUG_PRINTF_ARG
#define JDEBUG_PRINTF_ATTR __attribute__((format(printf, 2, 3)))
#endif

  void jdebug_log_printf(const logger_t* logger,
                         JDEBUG_PRINTF_ARG const char* fmt,
                         ...) JDEBUG_PRINTF_ATTR;

  void jdebug_log_deinit(logger_t* logger);

#ifdef __cplusplus
}
#endif

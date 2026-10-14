#pragma once

#include <jdebug.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef enum
  {
    FAIL_MODE_NONE,
    FAIL_MODE_SEGV,
    FAIL_MODE_FPE,
    FAIL_MODE_ABORT,
    FAIL_MODE_EXCEPTION
  } fail_mode_t;

  typedef struct
  {
    fail_mode_t fail_mode;
    bool thread;
    path_char_t* coredump_file;
    path_char_t* log_file;
  } example_params_t;

  int parse_options(int argc, path_char_t** argv, example_params_t* params);

#ifdef __cplusplus
}
#endif

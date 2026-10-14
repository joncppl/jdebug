#include <jdebug.h>

#include "common.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

// preventing optimizing away the fault
// and preserve stack traces
#ifdef _MSC_VER
#define NOINLINE __declspec(noinline)
#define NOOPTIMIZE
#else
#define NOINLINE __attribute__((noinline))
#define NOOPTIMIZE __attribute__((optimize("O0")))
#endif

#ifdef _MSC_VER
#pragma optimize("", off)
#endif

static void NOINLINE NOOPTIMIZE fail_segv()
{
  *(volatile int*) 0x0 = 0;
}

static void NOINLINE NOOPTIMIZE fail_fpe()
{
  volatile int y = 0;
  volatile int x = y / 0;
  (void) x;
}

static void NOINLINE NOOPTIMIZE fail_abort()
{
  abort();
}

static void NOINLINE NOOPTIMIZE fail(example_params_t* params)
{
  switch (params->fail_mode) {
    case FAIL_MODE_NONE:
      break;
    case FAIL_MODE_SEGV:
      fail_segv();
      break;
    case FAIL_MODE_FPE:
      fail_fpe();
      break;
    case FAIL_MODE_ABORT:
      fail_abort();
      break;
    case FAIL_MODE_EXCEPTION:
      fprintf(stderr, "exception failure only available in c++ example\n");
      exit(EXIT_FAILURE);
  }
}

#ifdef _MSC_VER
#pragma optimize("", on)
#endif


#ifdef _WIN32
#define THREAD_RETURN DWORD
#define main wmain
#else
#define THREAD_RETURN void*
#endif

static THREAD_RETURN NOINLINE thread_func(example_params_t* params)
{
  fail(params);
  return (THREAD_RETURN) 0;
}

int main(int argc, path_char_t* argv[])
{
  jdebug_crash_handler_early(argc, argv);

  example_params_t params;

  int exit_code = parse_options(argc, argv, &params);
  if (EXIT_SUCCESS != exit_code) {
    return exit_code;
  }

  jdebug_crash_handler_init_info_t init_info;
  jdebug_crash_handler_init_info_reset(&init_info);

  init_info.log_filename      = params.log_file;
  init_info.coredump_filename = params.coredump_file;

  jdebug_crash_handler_init(&init_info);

  if (params.thread) {
#ifdef _WIN32
    HANDLE thread = CreateThread(NULL,
                                 0,
                                 (LPTHREAD_START_ROUTINE) thread_func,
                                 &params,
                                 0,
                                 NULL);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_t thread;
    pthread_create(&thread, NULL, (void* (*) (void*) ) & thread_func, &params);
    pthread_join(thread, NULL);
#endif
  } else {
    fail(&params);
  }

  jdebug_crash_handler_deinit();
  return exit_code;
}

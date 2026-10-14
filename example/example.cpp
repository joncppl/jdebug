#include <jdebug.h>

#include "common.h"

#include <cstdlib>

#include <iostream>
#include <stdexcept>
#include <thread>

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

static void NOINLINE NOOPTIMIZE fail_exception()
{
  throw std::runtime_error("test exception");
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
      fail_exception();
      break;
  }
}

#ifdef _MSC_VER
#pragma optimize("", on)
#endif


#ifdef _WIN32
#define main wmain
#endif

int main(int argc, path_char_t* argv[])
{
  jdebug_crash_handler_early(argc, argv);

  example_params_t params;
  if (int exit_code = parse_options(argc, argv, &params); exit_code != EXIT_SUCCESS)
    return exit_code;

  jdebug::ScopedCrashHandlerInitInfo init_info;
  if (params.log_file)
    init_info.log_filename = params.log_file;
  if (params.coredump_file)
    init_info.coredump_filename = params.coredump_file;
  jdebug::ScopedCrashHandler crash_handler(init_info);

  if (params.thread) {
    std::thread thread([&] {
      fail(&params);
    });
    thread.join();
  } else {
    fail(&params);
  }

  return 0;
}

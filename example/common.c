#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <io.h>
#endif

static void print_help(void)
{
  printf("Usage: example [options]\n");
  printf("Options:\n");
  printf("  --help            Display this help message\n");
  printf("  --mode segv       Cause a segmentation fault\n");
  printf("         fpe        Cause a floating point exception\n");
  printf("         abort      Cause an abort\n");
  printf("         exception  Cause an exception (only available in C++ example)\n");
  printf("  --thread          Run in a separate thread\n");
  printf("  --log <file>      Log output to file\n");
  printf("  --core <file>     Generate core dump to file\n");

  exit(0);
}

#ifdef _WIN32
#define strcmp wcscmp
#define Fstr "%ls"
#define TEXT(s) L##s
#else
#define Fstr "%s"
#define TEXT(s) s
#endif

int parse_options(int argc, path_char_t** argv, example_params_t* params)
{
  params->fail_mode     = FAIL_MODE_NONE;
  params->thread        = 0;
  params->log_file      = NULL;
  params->coredump_file = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], TEXT("--help")) == 0) {
      print_help();
      return 0;
    }
    if (strcmp(argv[i], TEXT("--mode")) == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "no parameter to --mode\n");
        return -1;
      }

      i++;

      if (strcmp(argv[i], TEXT("segv")) == 0) {
        params->fail_mode = FAIL_MODE_SEGV;
      } else if (strcmp(argv[i], TEXT("fpe")) == 0) {
        params->fail_mode = FAIL_MODE_FPE;
      } else if (strcmp(argv[i], TEXT("abort")) == 0) {
        params->fail_mode = FAIL_MODE_ABORT;
      } else if (strcmp(argv[i], TEXT("exception")) == 0) {
        params->fail_mode = FAIL_MODE_EXCEPTION;
      } else {
        fprintf(stderr, "invalid parameter to --mode \"" Fstr "\"\n", argv[i]);
        fprintf(stderr, " should be one of \"segv\", \"fpe\", \"abort\", \"exception\"\n");
        return -1;
      }

    } else if (strcmp(argv[i], TEXT("--thread")) == 0) {
      params->thread = true;
    } else if (strcmp(argv[i], TEXT("--log")) == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "no parameter to --log\n");
        return -1;
      }
      params->log_file = argv[++i];
    } else if (strcmp(argv[i], TEXT("--core")) == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "no parameter to --core\n");
        return -1;
      }
      params->coredump_file = argv[++i];
    }
  }

  if (params->fail_mode == FAIL_MODE_NONE) {
    fprintf(stderr, "no --mode set\n");
    fprintf(stderr, " should be one of \"segv\", \"fpe\", \"abort\", \"exception\"\n");
    return -1;
  }

  return 0;
}

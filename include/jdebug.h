#pragma once

#include <stddef.h>

#ifdef _WIN32
#define path_char_t wchar_t
#else
#define path_char_t char
#endif

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct
  {
    const path_char_t* log_filename;
    const path_char_t* coredump_filename;
  } jdebug_crash_handler_init_info_t;

#define jdebug_crash_handler_init_info_reset(init_info_ptr_arg_)          \
  do {                                                                    \
    jdebug_crash_handler_init_info_t* init_info__ = (init_info_ptr_arg_); \
    init_info__->log_filename                     = NULL;                 \
    init_info__->coredump_filename                = NULL;                 \
  } while (0)

  void jdebug_crash_handler_early(int argc, path_char_t* argv[]);
  void jdebug_crash_handler_init(jdebug_crash_handler_init_info_t* init_info);
  void jdebug_crash_handler_deinit(void);

#ifdef __cplusplus
}

#include <string>

// <version> is only guaranteed to exist from c++20
#if defined(__has_include)
#if __has_include(<version>)
#include <version>
#endif
#endif

#if defined(__cpp_lib_filesystem) && __cpp_lib_filesystem >= 201703L
#include <filesystem>
#define JDEBUG_HAS_FILESYSTEM
#endif

namespace jdebug
{

#ifdef JDEBUG_HAS_FILESYSTEM
using path = std::filesystem::path;
#else
using path = std::basic_string<path_char_t>;
#endif

struct ScopedCrashHandlerInitInfo
{
  path log_filename {};
  path coredump_filename {};
};

class ScopedCrashHandler final
{
public:
  explicit ScopedCrashHandler(const ScopedCrashHandlerInitInfo& init_info = {})
  {
    jdebug_crash_handler_init_info_t c_init_info;
    jdebug_crash_handler_init_info_reset(&c_init_info);
    if (!init_info.log_filename.empty())
      c_init_info.log_filename = init_info.log_filename.c_str();
    if (!init_info.coredump_filename.empty())
      c_init_info.coredump_filename = init_info.coredump_filename.c_str();
    jdebug_crash_handler_init(&c_init_info);
  }

  ~ScopedCrashHandler()
  {
    jdebug_crash_handler_deinit();
  }

  ScopedCrashHandler(const ScopedCrashHandler&)            = delete;
  ScopedCrashHandler(ScopedCrashHandler&&)                 = delete;
  ScopedCrashHandler& operator=(const ScopedCrashHandler&) = delete;
  ScopedCrashHandler& operator=(ScopedCrashHandler&&)      = delete;
};

} // namespace jdebug
#endif

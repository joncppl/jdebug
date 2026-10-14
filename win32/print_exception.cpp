#include "log.h"

#include <windows.h>

#include <stdint.h>

#include <DbgHelp.h>

#include <exception>
#include <stdexcept>
#include <typeinfo>

#ifdef __GLIBCXX__
#include <cxxabi.h>
#endif

// https://devblogs.microsoft.com/oldnewthing/20100730-00/?p=13273
static void jdebug_print_vcruntime_exception(const logger_t* log, ULONG_PTR* exception_information)
{
#ifdef _M_X64
  const size_t b = exception_information[3];
#else
  const size_t b = 0;
#endif
  const size_t p2 = exception_information[2];

  if (((DWORD*) p2)[1]) {
    auto* e = reinterpret_cast<std::exception*>(exception_information[1]);
    jdebug_log_printf(log, " '%s' what(): \"%s\"", typeid(*e).name(), e->what());
  } else {
    const size_t p3               = ((DWORD*) p2)[3];
    const size_t p4               = ((DWORD*) (b + p3))[1];
    const DWORD p5                = ((DWORD*) (b + p4))[1];
    const char* mangled_type_name = (char*) (b + p5) + sizeof(void*) * 2;

    char name_buf[1024];
    ZeroMemory(name_buf, sizeof(name_buf));
    UnDecorateSymbolName(mangled_type_name + 1, name_buf, sizeof(name_buf), 0x2800);
    jdebug_log_printf(log, " '%s'", name_buf);
  }
}

#ifdef __GLIBCXX__
static_assert(sizeof(void*) == 8);

static void jdebug_print_glibcxx_exception(const logger_t* log, ULONG_PTR* ei)
{
  // ei[0] points to _Unwind_Exception inside __cxa_exception, and is followed by the exception object

  const char* eclz                  = *reinterpret_cast<const char**>(ei);
  const bool is_gnu_exception_class = eclz[7] == 'G' && eclz[6] == 'N' && eclz[5] == 'U'
                                      && eclz[4] == 'C' && eclz[3] == 'C' && eclz[2] == '+'
                                      && eclz[1] == '+' && (eclz[0] == '\0' || eclz[0] == '\x01');
  if (!is_gnu_exception_class)
    return;

  std::type_info* exception_type = static_cast<std::type_info*>(*(reinterpret_cast<void***>(ei)[0] - 10));
  void* exception_object         = reinterpret_cast<void***>(ei)[0] + 8;

  const char* mangled_type_name = exception_type->name();
  char* demangled_type_name     = abi::__cxa_demangle(mangled_type_name, nullptr, nullptr, nullptr);
  const char* type_name         = demangled_type_name ? demangled_type_name : mangled_type_name;

  const bool is_std_exception = typeid(std::exception).__do_catch(exception_type, static_cast<void**>(exception_object), 1);

  if (is_std_exception) {
    std::exception* ep = static_cast<std::exception*>(exception_object);
    jdebug_log_printf(log, " '%s' what(): \"%s\"", type_name, ep->what());
  } else {
    jdebug_log_printf(log, " '%s'", type_name);
  }

  free(demangled_type_name);
}
#endif

extern "C" void jdebug_print_cpp_exception(const logger_t* log, DWORD code, ULONG_PTR* exception_information)
{
  switch (code) {

    case 0xE06D7363:
      jdebug_print_vcruntime_exception(log, exception_information);
      break;

#ifdef __GLIBCXX__
    case 0x20474343: // used in mingw's libstdc++
      jdebug_print_glibcxx_exception(log, exception_information);
      break;
#endif
  }
}

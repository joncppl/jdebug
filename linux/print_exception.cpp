#include "jdebug_log.h"

#include <cxxabi.h>
#include <exception>

extern "C"
{
  char* jdebug_demangle_safe(const char* mangled_symbol);
  void jdebug_demangle_safe_free(const char* demangled);
}

namespace
{

class DemangledSymbol
{
public:
  DemangledSymbol()
      : m_mangled_symbol(nullptr)
      , m_demangled_symbol(nullptr)
  {}

  explicit DemangledSymbol(const char* mangled_symbol)
      : m_mangled_symbol(mangled_symbol)
      , m_demangled_symbol(jdebug_demangle_safe(mangled_symbol))
  {}

  DemangledSymbol(const DemangledSymbol& other)                = delete;
  DemangledSymbol(DemangledSymbol&& other) noexcept            = delete;
  DemangledSymbol& operator=(const DemangledSymbol& other)     = delete;
  DemangledSymbol& operator=(DemangledSymbol&& other) noexcept = delete;

  ~DemangledSymbol()
  {
    jdebug_demangle_safe_free(m_demangled_symbol);
  }

  [[nodiscard]] const char* get(const char* fallback = nullptr) const
  {
    if (m_demangled_symbol)
      return m_demangled_symbol;
    if (m_mangled_symbol)
      return m_mangled_symbol;
    return fallback;
  }

private:
  const char* m_mangled_symbol;
  char* m_demangled_symbol;
};

DemangledSymbol current_exception_type_name()
{
  if (const std::type_info* type_info = abi::__cxa_current_exception_type())
    return DemangledSymbol(type_info->name());
  return {};
}

} // namespace

extern "C" void jdebug_print_cpp_exception(const logger_t* log)
{
  const std::exception_ptr exception = std::current_exception();
  if (!exception)
    return;

  try {
    std::rethrow_exception(exception);
  } catch (const std::exception& e) {
    jdebug_log_format(log, "Uncaught C++ exception: '%s': \"%s\"\n",
                      current_exception_type_name().get("unknown type"),
                      e.what());
  } catch (...) {
    jdebug_log_format(log, "Uncaught C++ exception: '%s'",
                      current_exception_type_name().get("unknown type"));
  }
}

# jdebug

A library for debugging crashes in C/C++ applications.

Features:
 - Globally handle crashes, such as
   - Segmentation Fault
   - Floating Point Exceptions
   - Abort
   - Illegal Instruction
   - Uncaught C++ Exceptions
 - Write the cause of the crash, dump registers, dump mappings, and dump stack trace to stderr (and optionally a log file).
 - Print symbolic stack traces (using debug information if available)
 - Optionally Write a core/dump file that can be loaded in a debugger.

Supported Environments:

- Linux (x86-64)
  - gcc
  - clang
- Windows (x86-64)
  - MSVC
  - gcc (MSYS MinGW)


## Usage

### Requirements

#### Linux

- libdwarf (eg. `libdwarf-dev` package)
- libunwind (eg. `libunwind-dev` package)

#### MinGW

- libdwarf (eg. `mingw-w64-ucrt-x86_64-libdwarf` package)

### Build Samples

#### CMake

```bash
cd jdebug
cmake -B build -DCMAKE_BUILD_TYPE=DebWithRelInfo -DJDEBUG_ENABLE_EXAMPLES=ON
cmake --build build

# run examples
./build/example/example_c --help
./build/example/example_cpp --help
```

#### Meson

```bash
cd jdebug
meson setup build --buildtype debugoptimized -Dexamples=enabled
meson compile -C build

# run examples
./build/example/example_c --help
./build/example/example_cpp --help
```


### Include in Your Project

#### CMake

If included as a git submodule or verbatim, simply `add_subdirectory(jdebug)` and `target_link_libraries(my_target jdebug)`

Or with [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html),

```
include(FetchContent)

FetchContent_Declare(
    jdebug
    GIT_REPOSITORY https://github.com/joncppl/jdebug
    GIT_TAG v0.1.0
)
FetchContent_MakeAvailable(jdebug)

target_link_libraries(my_target jdebug)
```

#### Meson

Include as a [subproject](https://mesonbuild.com/Subprojects.html#using-a-subproject).

A [wrap](https://mesonbuild.com/Wrap-dependency-system-manual.html) may be used. For example,

```ini
[wrap-git]
url = https://github.com/joncppl/jdebug
revision = 0.1.0
depth = 1

[provide]
jdebug = jdebug_dep
```

### Use the Library

Initialization is global, and as such subsequent attempts to initialize will have no effect.
If initialization is invoked multiple times, an equal number of deinitialization invocations are needed to effect actual deinitialization.

#### C

```c
#include <jdebug.h>

int main(int argc, char* argv[])
{
  // `jdebug_crash_handler_early` must be the first thing called in main
  jdebug_crash_handler_early(argc, argv);


  // configure output files and init
  // `log_filename` and `coredump_filename` may be NULL if logging/coredump is not needed.
  jdebug_crash_handler_init_info_t init_info {
    .log_filename      = "myfile.log",
    .coredump_filename = "mydump.core",
  };
  jdebug_crash_handler_init(&init_info);

  // if neither logging/coredump is needed, NULL may be set.
  jdebug_crash_handler_init(NULL);


  // ... continue with application


  // shutdown (not strictly necessary)
  jdebug_crash_handler_deinit();

  return 0;
}
```

#### C++

```cpp
#include <jdebug.h>

int main(int argc, char* argv[])
{
  // `jdebug_crash_handler_early` must be the first thing called in main
  jdebug_crash_handler_early(argc, argv);

  // in c++17 or greater, jdebug is init with std::filesystem::path
  // otherwise, `std::string` or `std::wstring` on Linux and Windows respectively.
  std::filesystem::path log_filename      = "myfile.log";
  std::filesystem::path coredump_filename = "mydump.core";

  // configure output files and init
  jdebug::ScopedCrashHandlerInitInfo init_info {
    .log_filename = std::move(log_file),
    .coredump_filename = std::move(coredump_filename),
  };

  jdebug::ScopedCrashHandler crash_handler(init_info);

  // if neither logging/coredump is needed,
  jdebug::ScopedCrashHandler crash_handler;

  // keep crash_handler in scope for the duration of the application.


  // ... continue with application

  return 0;
}
```

#### Windows

To support paths containing extended characters on Windows, wide characters are used for paths.
As such you will need to implement `wmain` rather than `main` and provide `wchar_t*` paths to init.

```c
int wmain(int argc, wchar_t* argv[])
{
  jdebug_crash_handler_early(argc, argv);

  jdebug_crash_handler_init_info_t init_info;
  jdebug_crash_handler_init_info_reset(&init_info);

  init_info.log_filename      = L"myfile.log";
  init_info.coredump_filename = L"mydump.core";

  // ...
}
```

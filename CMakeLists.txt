cmake_minimum_required(VERSION 3.26)
project(jdebug VERSION 0.1.1)

option(JDEBUG_ENABLE_CXX_EXCEPTION_PRINTER
        "Enable c++ exception printer (requires linking to c++ standard library)"
        ON)

option(JDEBUG_ENABLE_EXAMPLES "Enable examples" ON)

if (UNIX)
    add_subdirectory(linux)
elseif (WIN32)
    add_subdirectory(win32)
endif ()

if (JDEBUG_ENABLE_EXAMPLES)
    add_subdirectory(example)
endif ()

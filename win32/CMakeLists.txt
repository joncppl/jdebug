add_library(jdebug STATIC
        jdebug.c
        log.c
        symbol.c)
target_compile_features(jdebug PRIVATE c_std_11)
target_include_directories(jdebug PUBLIC ../include)

if (CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(jdebug PRIVATE "/experimental:c11atomics")
    target_compile_options(jdebug PRIVATE "/W4")
elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(jdebug PRIVATE -Wall -Werror -Wpedantic)
endif ()


target_link_libraries(jdebug PUBLIC "psapi")
target_link_libraries(jdebug PUBLIC "dbghelp")

if (JDEBUG_ENABLE_CXX_EXCEPTION_PRINTER)
    target_sources(jdebug PRIVATE print_exception.cpp)
    target_compile_definitions(jdebug PRIVATE "JDEBUG_PRINT_EXCEPTION=1")
endif ()

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Dwarf REQUIRED IMPORTED_TARGET libdwarf)
    target_link_libraries(jdebug PUBLIC PkgConfig::Dwarf)
    target_compile_definitions(jdebug PRIVATE "JDEBUG_USE_LIBDWARF=1")
endif ()

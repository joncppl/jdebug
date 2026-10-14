add_library(jdebug STATIC
        child.c
        core.c
        jdebug.c
        jsignal.c
        log.c
        mapping.c
        stacktrace.c
        tasks.c
        util.c
)
target_compile_options(jdebug PRIVATE $<$<COMPILE_LANGUAGE:C>:-std=gnu11>)
target_include_directories(jdebug PUBLIC ../include)
target_compile_definitions(jdebug PRIVATE _GNU_SOURCE=1)
target_compile_options(jdebug PRIVATE -fvisibility=hidden)
target_compile_options(jdebug PRIVATE -Wall -Wextra -Wpedantic)

if (JDEBUG_ENABLE_CXX_EXCEPTION_PRINTER)
    target_sources(jdebug PRIVATE print_exception.cpp)
    target_compile_definitions(jdebug PRIVATE "JDEBUG_PRINT_EXCEPTION=1")
endif ()

if (NOT TARGET libunwind)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UNWIND REQUIRED libunwind-generic)

    target_link_libraries(jdebug PRIVATE ${UNWIND_LIBRARIES})
else ()
    target_link_libraries(jdebug PRIVATE libunwind)
endif ()

if (NOT TARGET libdwarf)
    find_library(DWARF_LIBRARY NAMES dwarf REQUIRED)
    target_link_libraries(jdebug PRIVATE ${DWARF_LIBRARY})
endif ()

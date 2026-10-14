sources = ['jdebug.c', 'log.c', 'symbol.c']
c_args = ['-experimental:c11atomics']

compiler = meson.get_compiler('c')
deps = [
    compiler.find_library('psapi'),
    compiler.find_library('dbghelp'),
]

if get_option('examples').enabled()
    sources += 'print_exception.cpp'
    c_args += '-DJDEBUG_PRINT_EXCEPTION=1'
endif

if compiler.get_id() == 'gcc'
    c_args += '-DJDEBUG_USE_LIBDWARF=1'
    deps += dependency('libdwarf', required: true)
endif


jdebug_lib = library(
    'jdebug',
    sources,
    include_directories : include_directories('../include'),
    c_args : c_args,
    dependencies: deps
)

jdebug_dep = declare_dependency(
    link_with : jdebug_lib,
    include_directories : include_directories('../include'),
)

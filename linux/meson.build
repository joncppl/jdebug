compiler = meson.get_compiler('c')

libunwind = dependency('libunwind-generic', required : false)
if not libunwind.found()
    libunwind = compiler.find_library('libunwind-generic', required : true)
endif

libdwarf = dependency('libdwarf', required : false)
if not libdwarf.found()
    libdwarf = compiler.find_library('dwarf', required : true)
endif

sources = [
    'child.c',
    'core.c',
    'jdebug.c',
    'jsignal.c',
    'log.c',
    'mapping.c',
    'stacktrace.c',
    'tasks.c',
    'util.c'
]
c_args = ['-D_GNU_SOURCE=1', '-fvisibility=hidden', '-std=gnu11']

if get_option('examples').enabled()
    sources += 'print_exception.cpp'
    c_args += '-DJDEBUG_PRINT_EXCEPTION=1'
endif

jdebug_lib = library(
    'jdebug',
    sources,
    dependencies : [libunwind, libdwarf],
    include_directories : include_directories('../include'),
    c_args : c_args,
    cpp_args : ['-fvisibility-inlines-hidden'],
)

jdebug_dep = declare_dependency(
    link_with : jdebug_lib,
    include_directories : include_directories('../include'),
)

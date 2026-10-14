args = []
link_args = []

if meson.get_compiler('c').get_id() == 'gcc'
    args += '-Wno-div-by-zero'

    if host_machine.system() == 'windows'
        link_args += '-municode'
    endif
endif

message(host_machine.system())
message(meson.get_compiler('c').get_id())

executable(
    'example_c',
    ['common.c', 'example.c'],
    dependencies : jdebug_dep,
    c_args : args,
    link_args : link_args,
)

executable(
    'example_cpp',
    ['common.c', 'example.cpp'],
    dependencies : jdebug_dep,
    cpp_args : args,
    link_args : link_args,
)

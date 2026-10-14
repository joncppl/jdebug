project('jdebug', ['c', 'cpp'],
        version : '0.1.1',
        default_options : [
            'warning_level=1',
            'cpp_std=c++17',
            'c_std=c11',
            'default_library=static'
        ])

if host_machine.system() == 'windows'
    subdir('win32')
elif host_machine.system() == 'linux'
    subdir('linux')
endif

if get_option('examples').enabled()
    subdir('example')
endif

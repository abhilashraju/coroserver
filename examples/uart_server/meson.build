uart_server_exe = executable(
    'uart_server',
    'uart_server.cpp',
    include_directories: reactor_inc,
    dependencies: [reactor_dep],
    install: true,
    install_dir: get_option('bindir')
)
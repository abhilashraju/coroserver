# Console Server executable
console_server = executable('console_server',
  'console_server.cpp',
  include_directories: reactor_inc,
  dependencies: [reactor_dep,sdbusplus_dep],
  install: true,
  install_dir: get_option('bindir')
)

# Console Client executable
console_client = executable('console_client',
  'console_client.cpp',
  include_directories: reactor_inc,
  dependencies: [reactor_dep],
  install: true,
  install_dir: get_option('bindir')
)
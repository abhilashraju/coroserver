

# Define the executable
executable('coroserver',
  'sample_server.cpp',
  dependencies: [reactor_dep,sdbusplus_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin'
)

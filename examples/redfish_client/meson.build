
# Define the executable

executable('redfish_client',
  'redfish_client.cpp',
  dependencies: [reactor_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin'
)

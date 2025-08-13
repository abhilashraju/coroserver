

# Define the executable
#boost_filesystem_dep = dependency('boost', modules: ['filesystem'])
executable('lldp_discoverd',
  'lldp_discoverd.cpp',
  dependencies: [reactor_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin'
)
install_data(
    'service/lldp_discoverd.service',
    install_dir: '/etc/systemd/system',
)

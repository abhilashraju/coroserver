

# Define the executable
executable('coroserver',
  'sample_server.cpp',
  dependencies: [reactor_dep,sdbusplus_dep],
  install: true,
  install_dir: '/usr/bin'
)

install_data('server-cert.pem', install_dir: '/tmp')
install_data('server-key.pem', install_dir: '/tmp')
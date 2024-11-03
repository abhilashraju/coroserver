

# Define the executable
executable('coroserver',
  'sample_server.cpp',
  dependencies: [coroserver_dep],
  install: true,
  install_dir: '/usr/bin'
)

install_data('server-cert.pem', install_dir: '/etc/ssl/private')
install_data('server-key.pem', install_dir: '/etc/ssl/private')
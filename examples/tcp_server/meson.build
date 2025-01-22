
# Define the executable

executable('tcp_server',
  'tcp_server.cpp',
  dependencies: [reactor_dep],
  install: true,
  install_dir: '/usr/bin'
)
install_data('server-cert.pem', install_dir: '/etc/ssl/private')
install_data('server-key.pem', install_dir: '/etc/ssl/private')

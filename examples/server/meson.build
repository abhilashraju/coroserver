

# Define the executable
sdbusplus_dep = dependency('sdbusplus', required: true, include_type: 'system')
executable('coroserver',
  'sample_server.cpp',
  dependencies: [reactor_dep,sdbusplus_dep],
  install: true,
  install_dir: '/usr/bin'
)

install_data('server-cert.pem', install_dir: '/etc/ssl/private')
install_data('server-key.pem', install_dir: '/etc/ssl/private')
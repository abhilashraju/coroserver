
# Define the executable
include_dirs =['includes']
libarchive_dep = dependency('libarchive')
executable('event_broker',
  'event_broker.cpp',
  dependencies: [reactor_dep,sdbusplus_dep,libarchive_dep],
  include_directories: include_dirs,
  install: true,
  install_dir: '/usr/bin'
)
install_data(
    'service/event_broker.service',
    install_dir: '/etc/systemd/system',
)
install_data('server-cert.pem', install_dir: '/tmp')
install_data('server-key.pem', install_dir: '/tmp')
subdir('plugins')
subdir('publisher')
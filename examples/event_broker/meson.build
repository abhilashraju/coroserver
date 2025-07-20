
# Define the executable
include_dirs =['includes']
libarchive_dep = dependency('libarchive')
executable('event_broker',
  'event_broker.cpp',
  dependencies: [reactor_dep,sdbusplus_dep,libarchive_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  include_directories: include_dirs,
  install: true,
  install_dir: '/usr/bin'
)
install_data(
    'service/event_broker.service',
    install_dir: '/etc/systemd/system',
)
subdir('plugins')
subdir('publisher')
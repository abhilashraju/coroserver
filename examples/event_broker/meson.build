
# Define the executable
include_dirs =['includes']
executable('event_broker',
  'event_broker.cpp',
  dependencies: [reactor_dep,sdbusplus_dep],
  include_directories: include_dirs,
  install: true,
  install_dir: '/usr/bin'
)
subdir('plugins')
subdir('publisher')
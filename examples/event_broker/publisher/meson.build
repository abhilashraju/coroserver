include_dirs =['../includes']
executable('event_publisher',
  'event_publisher.cpp',
  dependencies: [reactor_dep,sdbusplus_dep],
  include_directories: include_dirs,
  install: true,
  install_dir: '/usr/lib/plugins'
)
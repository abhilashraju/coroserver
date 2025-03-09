include_dirs =['../includes']
library('event_broker_plugin',
  'event_broker_plugin.cpp',
  dependencies: [reactor_dep,sdbusplus_dep,libarchive_dep],
  include_directories: include_dirs,
  install: true,
  install_dir: '/usr/lib/plugins'
)
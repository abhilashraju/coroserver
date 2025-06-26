include_dirs =['../../includes']
library('rsync_plugin',
  'rsync_plugin.cpp',
  dependencies: [reactor_dep,sdbusplus_dep,libarchive_dep],
  include_directories: include_dirs,
  install: true,
  install_dir: '/usr/lib/plugins'
)
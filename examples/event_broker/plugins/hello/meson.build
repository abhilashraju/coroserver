include_dirs =['../../includes']
library('hello_plugin',
  'hello_plugin.cpp',
  dependencies: [reactor_dep,sdbusplus_dep,libarchive_dep],
  include_directories: include_dirs,
  install: true,
  install_dir: '/usr/lib/plugins'
)
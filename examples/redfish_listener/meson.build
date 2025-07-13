

# Define the executable
#boost_filesystem_dep = dependency('boost', modules: ['filesystem'])
executable('redfish_listener',
  'redfishlistener.cpp',
  dependencies: [reactor_dep],
  install: true,
  install_dir: '/usr/bin'
)
install_data(
    'service/redfishlistener.service',
    install_dir: '/etc/systemd/system',
)

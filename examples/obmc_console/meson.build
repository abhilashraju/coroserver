# Console Server executable
console_server = executable('console_server',
  'console_server_multi.cpp',
  include_directories: reactor_inc,
  dependencies: [reactor_dep,sdbusplus_dep],
  install: true,
  install_dir: get_option('bindir')
)

# Console Client executable
console_client = executable('console_client',
  'console_client.cpp',
  include_directories: reactor_inc,
  dependencies: [reactor_dep],
  install: true,
  install_dir: get_option('bindir')
)

# Install systemd service file
systemd = dependency('systemd', required: false)
if systemd.found()
  systemd_system_unit_dir = systemd.get_variable(
    pkgconfig: 'systemdsystemunitdir',
    pkgconfig_define: ['prefix', get_option('prefix')]
  )
  
  install_data(
    'console_server.service',
    install_dir: systemd_system_unit_dir
  )
endif

# Install example configuration file
install_data(
  'obmc-console-multi.conf',
  install_dir: get_option('sysconfdir'),
  rename: 'obmc-console-multi.conf'
)
# Find libssh2 dependency (optional for SSH remote console support)
libssh2_dep = dependency('libssh2', required: false)

# Build dependencies list
console_deps = [reactor_dep, sdbusplus_dep]
if libssh2_dep.found()
  console_deps += libssh2_dep
  message('libssh2 found - SSH remote console support enabled')
else
  warning('libssh2 not found - SSH remote console support disabled')
endif

# Console Server executable
console_server = executable('console_server',
  'console_server_multi.cpp',
  include_directories: reactor_inc,
  dependencies: console_deps,
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

# Install example configuration files
install_data(
  'obmc-console-multi.conf',
  install_dir: get_option('sysconfdir'),
  rename: 'obmc-console-multi.conf'
)

# Install SSH remote console configuration
install_data(
  'obmc-console-ssh-remote.conf',
  install_dir: get_option('sysconfdir') / 'obmc-console',
  rename: 'obmc-console-ssh-remote.conf'
)

# Install sshd configuration for port 2202
install_data(
  'sshd.2202.conf',
  install_dir: get_option('sysconfdir') / 'obmc-console',
  rename: 'sshd.2202.conf'
)

# Install systemd service for SSH console on port 2202
if systemd.found()
  install_data(
    'obmc-console-ssh@2202.service',
    install_dir: systemd_system_unit_dir,
    rename: 'obmc-console-ssh@2202.service'
  )
endif
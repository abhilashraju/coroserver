

# Define the executable

systemd_system_unit_dir = systemd.get_variable('systemdsystemunitdir')
executable('dbus_inspector',
  'dbus_inspector.cpp',
  dependencies: [reactor_dep,sdbusplus_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin'
)
configure_file(
    
    input: 'config/dbus_inspector.service.in',
    output: 'dbus_inspector.service',
    install_dir: systemd_system_unit_dir,
    install: true,
    configuration: configuration_data({
        'MESON_INSTALL_PREFIX': '/usr',
    }),
)
install_data('server-cert.pem', install_dir: '/etc/ssl/private')
install_data('server-key.pem', install_dir: '/etc/ssl/private')
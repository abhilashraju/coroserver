
# Define the executable

executable('spdmlite',
  'spdmlite.cpp',
  dependencies: [reactor_dep,sdbusplus_dep],
  install: true,
  install_dir: '/usr/bin'
)
subdir('certapp')
install_data(
    'service/xyz.openbmc_project.spdmlite.service',
    install_dir: '/etc/systemd/system',
)
install_data(
    'service/xyz.openbmc_project.spdmlite.conf',
    install_dir: '/etc/dbus-1/system.d/',
)
install_data(
    'spdm.conf',
    install_dir: '/var/spdm/',
)
install_data(
    'certs/ca.pem',
    install_dir: '/etc/ssl/certs',
)
install_data(
    'certs/client_cert.pem',
    'certs/server_cert.pem',
    'certs/public.pem',
    install_dir: '/etc/ssl/certs/https',
)
install_data(
    'certs/client_key.pem',
    'certs/server_key.pem',
    'certs/ca_key.pem',
    install_dir: '/etc/ssl/private',
)
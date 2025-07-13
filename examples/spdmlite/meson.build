
# Define the executable

executable('spdmlite',
  'spdmlite.cpp',
  dependencies: [reactor_dep,sdbusplus_dep],
  install: true,
  install_dir: '/usr/bin'
)
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
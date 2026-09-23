# FirmwareUpdater — OpenBMC firmware polling and download daemon

systemd_system_unit_dir = systemd.get_variable('systemdsystemunitdir')

executable(
  'firmware_updater',
  ['firmware_updater.cpp'],
  dependencies: [reactor_dep, sdbusplus_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin',
)

install_data(
  'config/firmware_updater.json',
  install_dir: '/etc/firmware_updater',
)

install_data(
  'service/firmware_updater.service',
  install_dir: systemd_system_unit_dir,
)

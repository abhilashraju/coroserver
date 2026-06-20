# I2C Service executable
executable('i2c-service',
  'i2c_service.cpp',
  dependencies: [reactor_dep, sdbusplus_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin'
)

# Install systemd service file
install_data(
  'com.ibm.I2CService.service',
  install_dir: systemd.get_variable('systemdsystemunitdir')
)
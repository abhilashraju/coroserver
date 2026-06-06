# Define the executable for Redfish Proxy
executable('redfishproxy',
  'redfish_proxy.cpp',
  dependencies: [reactor_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin'
)

# Install systemd service file
install_data(
    'service/redfishproxy.service',
    install_dir: '/etc/systemd/system',
)
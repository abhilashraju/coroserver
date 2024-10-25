project('coroserver', 'cpp', version: '1.0.0', default_options: ['cpp_std=c++20'])

# Find Boost dependency
boost_dep = dependency('boost', required: true)
openssl_dep = dependency('openssl', required: true)
sdbusplus_dep = dependency('sdbusplus')
nlohmann_json_dep = dependency('nlohmann_json', required: true)

# Define the executable
executable('coroserver',
  'sample_server.cpp',
  include_directories: include_directories('include'),
  dependencies: [boost_dep, openssl_dep, sdbusplus_dep, nlohmann_json_dep],
  install: true,
  install_dir: '/usr/bin'
)

install_data('server-cert.pem', install_dir: '/etc/ssl/private')
install_data('server-key.pem', install_dir: '/etc/ssl/private')
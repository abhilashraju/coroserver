project('coroserver', 'cpp', version: '1.0.0', default_options: ['cpp_std=c++20'])

# Find Boost dependency
boost_dep = dependency('boost', required: true)
openssl_dep = dependency('openssl', required: true)
sdbusplus_dep = dependency('sdbusplus', required: true, include_type: 'system')
#if not sdbusplus_dep.found()
#    sdbusplus_proj = subproject('sdbusplus', required: true)
#    sdbusplus_dep = sdbusplus_proj.get_variable('sdbusplus_dep')
#    sdbusplus_dep = sdbusplus_dep.as_system('system')
#endif
nlohmann_json_dep = dependency(
    'nlohmann_json',
    version: '>=3.11.3',
    fallback: ['nlohmann_json', 'nlohmann_json_dep'],
    include_type: 'system',
)

if not nlohmann_json_dep.found()
    nlohmann_json_proj = subproject('nlohmann_json', required: true)
    nlohmann_json_dep = nlohmann_json_proj.get_variable('nlohmann_json_dep')
endif

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
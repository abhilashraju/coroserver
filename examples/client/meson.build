
# Define the executable
# Add dependencies
add_project_arguments(
    cxx.get_supported_arguments(['-DBOOST_ASIO_DISABLE_THREADS']),language: 'cpp')
executable('coroclient',
  'sample_client.cpp',
  dependencies: [boost_dep, openssl_dep, sdbusplus_dep, nlohmann_json_dep,coroserver_dep],
  install: true,
  install_dir: '/usr/bin'
)

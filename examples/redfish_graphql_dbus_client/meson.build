# GraphQL Redfish → DBus Satellite Bridge

graphql_inc = include_directories('../graphql_server/include')

executable('graphql_dbus_client',
  ['graphql_dbus_client.cpp'],
  dependencies: [reactor_dep, sdbusplus_dep],
  include_directories: graphql_inc,
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin'
)

install_data(
  'config/satellite_queries.json',
  install_dir: '/etc/graphql_dbus_client'
)

install_data(
  'service/graphql_dbus_client.service',
  install_dir: '/etc/systemd/system'
)

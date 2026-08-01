# GraphQL Server Example

# Build simple GraphQL server (no external dependencies)
# executable('graphql_server',
#   'graphql_server.cpp',
#   dependencies: [reactor_dep],
#   cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
#   install: true,
#   install_dir: '/usr/bin'
# )

graphql_inc = include_directories('include')

executable('graphql_parser_test',
  ['graphql_parser_test.cpp'],
  dependencies: [reactor_dep],
  include_directories: graphql_inc,
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: false
)

executable('graphql_redfish_server',
  ['graphql_redfish_server.cpp',
   'graphql_redfish_executor.cpp',
   'graphql_redfish_provider.cpp',
   'graphql_redfish_schema.cpp'],
  dependencies: [reactor_dep],
  include_directories: graphql_inc,
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin'
)

install_data(
  'service/graphql_redfish_server.service',
  install_dir: '/etc/systemd/system'
)

message('GraphQL server handwritten parser build configured')

message('GraphQL server build configured')
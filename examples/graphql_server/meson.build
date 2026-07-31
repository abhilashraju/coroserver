# GraphQL Server Example

# Build simple GraphQL server (no external dependencies)
# executable('graphql_server',
#   'graphql_server.cpp',
#   dependencies: [reactor_dep],
#   cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
#   install: true,
#   install_dir: '/usr/bin'
# )

# Build GraphQL server with in-repo handwritten parser
executable('graphql_server_libgraphql',
  ['graphql_server_libgraphql.cpp', 'graphql_parser_libgraphql.cpp'],
  dependencies: [reactor_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin'
)

executable('graphql_parser_libgraphql_test',
  ['graphql_parser_libgraphql_test.cpp', 'graphql_parser_libgraphql.cpp'],
  dependencies: [reactor_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: false
)

executable('graphql_redfish_server',
  ['graphql_redfish_server.cpp',
   'graphql_redfish_executor.cpp',
   'graphql_redfish_provider.cpp',
   'graphql_redfish_schema.cpp',
   'graphql_parser_libgraphql.cpp'],
  dependencies: [reactor_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin'
)

message('GraphQL server handwritten parser build configured')

message('GraphQL server build configured')
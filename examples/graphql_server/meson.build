# GraphQL Server Example

# Build simple GraphQL server (no external dependencies)
# executable('graphql_server',
#   'graphql_server.cpp',
#   dependencies: [reactor_dep],
#   cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
#   install: true,
#   install_dir: '/usr/bin'
# )

# Build GraphQL server with libgraphqlparser
libgraphqlparser_dep = dependency('libgraphqlparser', required: false)

if libgraphqlparser_dep.found()
  executable('graphql_server_libgraphql',
    ['graphql_server_libgraphql.cpp', 'graphql_parser_libgraphql.cpp'],
    dependencies: [reactor_dep, libgraphqlparser_dep],
    cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
    install: true,
    install_dir: '/usr/bin'
  )
  message('GraphQL server with libgraphqlparser build configured')
else
  message('libgraphqlparser not found, skipping graphql_server_libgraphql')
endif

message('GraphQL server build configured')
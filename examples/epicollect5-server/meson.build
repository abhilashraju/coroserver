# Epicollect5-compatible HTTP server example
#
# sqlite3 is bundled as the official amalgamation (sqlite3.c + sqlite3.h)
# so no system sqlite3 dev package is required.

sqlite3_lib = static_library('sqlite3_bundled',
  ['sqlite3.c'],
  c_args: [
    '-DSQLITE_THREADSAFE=0',
    '-DSQLITE_DEFAULT_MEMSTATUS=0',
  ],
  install: false,
)

sqlite3_dep = declare_dependency(
  link_with: sqlite3_lib,
  include_directories: include_directories('.'),
)

executable('epicollect5_server',
  ['main.cpp', 'db.cpp', 'jwt_service.cpp', 'auth_handlers.cpp',
   'project_handlers.cpp', 'entry_handlers.cpp',
   'multipart_parser.cpp', 'media_handlers.cpp',
   'utility_handlers.cpp', 'admin_handlers.cpp'],
  dependencies: [reactor_dep, sqlite3_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: false,
)

message('epicollect5_server build configured')

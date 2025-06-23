

# Define the executable
#boost_filesystem_dep = dependency('boost', modules: ['filesystem'])
executable('journalserver',
  'journal_server.cpp',
  dependencies: [reactor_dep],
  install: true,
  install_dir: '/usr/bin'
)
install_data(
    'service/journalserver.service',
    install_dir: '/etc/systemd/system',
)
install_data('server-cert.pem', install_dir: '/tmp')
install_data('server-key.pem', install_dir: '/tmp')
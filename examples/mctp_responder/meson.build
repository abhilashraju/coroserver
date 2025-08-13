

# Define the executable

executable('mctp_responder',
  'mctp_responder.cpp',
  dependencies: [reactor_dep],
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin'
)
#install_data(
#    'service/journalserver.service',
#    install_dir: '/etc/systemd/system',
#)

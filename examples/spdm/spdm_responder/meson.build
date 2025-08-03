
# Define the executable

executable('spdm_responder',
  [ '../third_party/spdm_device_secret_lib_sample/lib.c',
  '../third_party/debuglib/libspdm_com_support.c',
  '../third_party/debuglib/debuglib.c',
  'spdm_responder.cpp'],
  dependencies: spdm_dep,
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  include_directories:incdir,
  install: true,
  install_dir: '/usr/bin'
)

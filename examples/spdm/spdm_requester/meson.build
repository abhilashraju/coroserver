
# Define the executable

executable('spdm_requester',
  [ '../third_party/spdm_device_secret_lib_sample/lib.c',
  '../third_party/debuglib/libspdm_com_support.c',
  '../third_party/debuglib/debuglib.c',
  '../third_party/spdm_device_secret_lib_sample/lib.c',
  '../third_party/spdm_device_secret_lib_sample/libspdm_support.c',
  '../third_party/spdm_device_secret_lib_sample/read_pub_key_der.c',
  '../third_party/spdm_device_secret_lib_sample/read_pub_cert.c',
  'spdm_requester.cpp'],
  dependencies: spdm_dep,
  include_directories:incdir,
  cpp_args: ['-UDBOOST_ASIO_DISABLE_THREADS'],
  install: true,
  install_dir: '/usr/bin'
)

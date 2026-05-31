
# Define the executable

# Build C files as a static library first
spdm_responder_c_lib = static_library('spdm_responder_c',
  [
    '../third_party/spdm_device_secret_lib_sample/lib.c',
    '../third_party/spdm_device_secret_lib_sample/libspdm_support.c',
    '../third_party/debuglib/debuglib.c',
  ],
  dependencies: spdm_dep,
  include_directories: incdir,
  c_args: ['-Wno-unused-parameter', '-Wno-unused-variable'],
)

executable('spdm_responder',
  ['src/spdm_responder.cpp'],
  dependencies: spdm_dep,
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  include_directories:[incdir,'src'],
  link_with: spdm_responder_c_lib,
  install: true,
  install_dir: '/usr/bin'
)
configure_file(
    input: 'service/xyz.openbmc_project.spdm.responder.service.in',
    output: 'xyz.openbmc_project.spdm.responder.service',
    configuration: {
        'SYSTEMD_TARGET': 'multi-user.target',
        'DEFAULT_BUSNAME': 'xyz.openbmc_project.spdm.responder',
    },
    install: true,
    install_dir: dependency('systemd').get_variable('systemdsystemunitdir'),
)
install_data(
    'service/xyz.openbmc_project.spdm.responder.conf',
    install_dir: '/etc/dbus-1/system.d/',
)
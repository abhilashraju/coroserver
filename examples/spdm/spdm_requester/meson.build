
# Define the executable

# Build C files as a static library first
spdm_requester_c_lib = static_library('spdm_requester_c',
  [
    '../third_party/spdm_device_secret_lib_sample/lib.c',
    '../third_party/spdm_device_secret_lib_sample/libspdm_support.c',
    '../third_party/debuglib/debuglib.c',
  ],
  dependencies: spdm_dep,
  include_directories: incdir,
  c_args: ['-Wno-unused-parameter', '-Wno-unused-variable'],
)

executable('spdm_requester',
  ['src/spdm_requester.cpp'],
  dependencies: spdm_dep,
  include_directories:[incdir,'src','gen'],
  cpp_args: ['-UDBOOST_ASIO_DISABLE_THREADS'],
  link_with: spdm_requester_c_lib,
  install: true,
  install_dir: '/usr/bin'
)
configure_file(
    input: 'service/xyz.openbmc_project.spdm.requester.service.in',
    output: 'xyz.openbmc_project.spdm.requester.service',
    configuration: {
        'SYSTEMD_TARGET': 'multi-user.target',
        'DEFAULT_BUSNAME': 'xyz.openbmc_project.spdm.requester',
    },
    install: true,
    install_dir: dependency('systemd').get_variable('systemdsystemunitdir'),
)
install_data(
    'service/xyz.openbmc_project.spdm.requester.conf',
    install_dir: '/etc/dbus-1/system.d/',
)
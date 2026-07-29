
# Async SPDM requester — standalone binary extracted from spdm_requester/

# Build C files as a static library first
async_requester_c_lib = static_library('async_requester_c',
  [
    '../third_party/spdm_device_secret_lib_sample/lib.c',
    '../third_party/spdm_device_secret_lib_sample/libspdm_support.c',
    '../third_party/debuglib/debuglib.c',
  ],
  dependencies: spdm_dep,
  include_directories: incdir,
  c_args: ['-Wno-unused-parameter', '-Wno-unused-variable'],
)

async_requester_inc = include_directories('src', '../spdm_requester/src', '../spdm_requester/gen')

executable('spdm_requester_async',
  ['src/spdm_requester_async.cpp'],
  dependencies: spdm_dep,
  include_directories: [incdir, async_requester_inc],
  cpp_args: ['-UDBOOST_ASIO_DISABLE_THREADS'],
  link_with: async_requester_c_lib,
  install: true,
  install_dir: '/usr/bin'
)
configure_file(
    input: 'service/xyz.openbmc_project.spdm.async_requester.service.in',
    output: 'xyz.openbmc_project.spdm.async_requester.service',
    configuration: {
        'SYSTEMD_TARGET': 'multi-user.target',
        'DEFAULT_BUSNAME': 'xyz.openbmc_project.spdm.async_requester',
    },
    install: true,
    install_dir: dependency('systemd').get_variable('systemdsystemunitdir'),
)
install_data(
    'service/xyz.openbmc_project.spdm.async_requester.conf',
    install_dir: '/etc/dbus-1/system.d/',
)

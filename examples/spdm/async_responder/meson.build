
# Async SPDM responder — standalone binary extracted from spdm_responder/

# Build C files as a static library first
async_responder_c_lib = static_library('async_responder_c',
  [
    '../third_party/spdm_device_secret_lib_sample/lib.c',
    '../third_party/spdm_device_secret_lib_sample/libspdm_support.c',
    '../third_party/debuglib/debuglib.c',
  ],
  dependencies: spdm_dep,
  include_directories: incdir,
  c_args: ['-Wno-unused-parameter', '-Wno-unused-variable'],
)

async_responder_inc = include_directories('src', '../spdm_responder/src')

executable('spdm_responder_async',
  ['src/spdm_responder_async.cpp'],
  dependencies: spdm_dep,
  cpp_args: ['-DBOOST_ASIO_DISABLE_THREADS'],
  include_directories: [incdir, async_responder_inc],
  link_with: async_responder_c_lib,
  install: true,
  install_dir: '/usr/bin'
)
configure_file(
    input: 'service/xyz.openbmc_project.spdm.async_responder.service.in',
    output: 'xyz.openbmc_project.spdm.async_responder.service',
    configuration: {
        'SYSTEMD_TARGET': 'multi-user.target',
        'DEFAULT_BUSNAME': 'xyz.openbmc_project.spdm.async_responder',
    },
    install: true,
    install_dir: dependency('systemd').get_variable('systemdsystemunitdir'),
)
install_data(
    'service/xyz.openbmc_project.spdm.async_responder.conf',
    install_dir: '/etc/dbus-1/system.d/',
)

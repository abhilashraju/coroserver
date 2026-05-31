
libspdm_dep = dependency('libspdm', required: false)
if not libspdm_dep.found()
    message('libspdm pkg-config not found, trying manual library detection')
    # Try to find the library manually using actual library names from OpenBMC
    libspdm_common = meson.get_compiler('cpp').find_library('spdm_common_lib', required: false)
    libspdm_requester = meson.get_compiler('cpp').find_library('spdm_requester_lib', required: false)
    libspdm_responder = meson.get_compiler('cpp').find_library('spdm_responder_lib', required: false)
    libspdm_crypt = meson.get_compiler('cpp').find_library('spdm_crypt_lib', required: false)
    libspdm_secured_message = meson.get_compiler('cpp').find_library('spdm_secured_message_lib', required: false)
    libspdm_transport_tcp = meson.get_compiler('cpp').find_library('spdm_transport_tcp_lib', required: false)
    libspdm_device_secret = meson.get_compiler('cpp').find_library('spdm_device_secret_lib_sample', required: false)
    libspdm_crypt_ext = meson.get_compiler('cpp').find_library('spdm_crypt_ext_lib', required: false)
    libcryptlib_openssl = meson.get_compiler('cpp').find_library('cryptlib_openssl', required: false)
    libmemlib = meson.get_compiler('cpp').find_library('memlib', required: false)
    libmalloclib = meson.get_compiler('cpp').find_library('malloclib', required: false)
    libplatform_lib = meson.get_compiler('cpp').find_library('platform_lib', required: false)
    librnglib = meson.get_compiler('cpp').find_library('rnglib', required: false)
    libdebuglib = meson.get_compiler('cpp').find_library('debuglib', required: false)

    if libspdm_common.found() and libspdm_requester.found() and libspdm_responder.found() and libspdm_crypt.found() and libspdm_transport_tcp.found() and libspdm_device_secret.found() and libspdm_crypt_ext.found() and libcryptlib_openssl.found() and libmemlib.found() and libmalloclib.found() and libplatform_lib.found() and librnglib.found() and libdebuglib.found()
        message('Found libspdm libraries manually')
        # For cross-compilation, we need to use -isystem with sysroot-relative path
        # The compiler already has --sysroot set, so paths starting with = are relative to sysroot
        libspdm_dep = declare_dependency(
            dependencies: [
                libspdm_common,
                libspdm_requester,
                libspdm_responder,
                libspdm_crypt,
                libspdm_secured_message,
                libspdm_transport_tcp,
                libspdm_device_secret,
                libspdm_crypt_ext,
                libcryptlib_openssl,
                libmemlib,
                libmalloclib,
                libplatform_lib,
                librnglib,
                libdebuglib,
            ],
            compile_args: ['-isystem', '=/usr/include/libspdm'],
        )
    else
        error('libspdm libraries not found. Please ensure libspdm is installed.')
    endif
endif

incdir = include_directories(
    'third_party/',
    'include',
    'third_party/spdm_device_secret_lib_sample',
)

spdm_dep = declare_dependency(
    include_directories: [reactor_inc, incdir],
    dependencies: [
        sdbusplus_dep,
        sdeventplus_dep,
        dependency('phosphor-logging'),
        openssl_dep,
        boost_dep,
        dependency('phosphor-dbus-interfaces'),
        libspdm_dep,
    ],
)

subdir('spdm_responder')
subdir('spdm_requester')
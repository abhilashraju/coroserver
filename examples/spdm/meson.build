libspdm = dependency('libspdm')

incdir = include_directories(
  'third_party/',
  'includes',
  'third_party/spdm_device_secret_lib_sample',
)
phosphor_logging_dep = dependency('phosphor-logging')
spdm_dep = declare_dependency(
    include_directories:incdir,
    dependencies:[reactor_dep,phosphor_logging_dep,nlohmann_json_dep,openssl_dep,boost_dep,sdbusplus_dep,libspdm]
)
subdir('spdm_responder')
subdir('spdm_requester')
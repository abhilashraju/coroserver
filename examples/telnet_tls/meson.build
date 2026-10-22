# Telnet over TLS console gateway (telnet_tls_server / telnet_tls_client).
# Needs PAM for the in-session login; skipped when libpam is not available.
cxx_telnet = meson.get_compiler('cpp')
pam_dep = dependency('pam', required: false)
if not pam_dep.found()
  pam_dep = cxx_telnet.find_library('pam', required: false)
endif

if not pam_dep.found()
  warning('libpam not found - telnet_tls example disabled')
else
  # console_config.hpp lives next to the console server sources.
  console_config_inc = include_directories('../obmc_console')

  telnet_tls_server = executable('telnet_tls_server',
    'telnet_tls_server.cpp',
    include_directories: [reactor_inc, console_config_inc],
    dependencies: [reactor_dep, pam_dep],
    install: true,
    install_dir: get_option('bindir')
  )

  telnet_tls_client = executable('telnet_tls_client',
    'telnet_tls_client.cpp',
    include_directories: reactor_inc,
    dependencies: [reactor_dep],
    install: true,
    install_dir: get_option('bindir')
  )

  # PAM service used for the telnet login prompt.
  install_data(
    'telnet-tls.pam',
    install_dir: get_option('sysconfdir') / 'pam.d',
    rename: 'telnet-tls'
  )

  telnet_systemd = dependency('systemd', required: false)
  if telnet_systemd.found()
    telnet_systemd_unit_dir = telnet_systemd.get_variable(
      pkgconfig: 'systemdsystemunitdir',
      pkgconfig_define: ['prefix', get_option('prefix')]
    )
    install_data(
      'telnet_tls_server.service',
      install_dir: telnet_systemd_unit_dir
    )
  endif
endif

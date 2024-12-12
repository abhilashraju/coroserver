subdir('web_client')
subdir('web_crawler')
subdir('test')
subdir('when_all')
subdir('unix_client')
subdir('body_conversion')
subdir('gerrit_scripts')

sdbusplus_dep = dependency('sdbusplus', required: false, include_type: 'system')
systemd = dependency('systemd',required: false)
if sdbusplus_dep.found() and systemd.found()
    subdir('server')
    subdir('dbus_inspector')
endif
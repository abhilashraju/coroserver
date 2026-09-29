
# ── DbusWatcher unit tests ────────────────────────────────────────────────────
#
# These tests target the DbusWatcher / DbusPropertyWatcher / DbusSignalWatcher
# logic in include/dbusproperty_watcher.hpp without requiring a running D-Bus
# daemon.  A TestWatcher subclass injects signals and timeouts directly by
# calling notifyChange() and cancelWatch() on the base class.
#
# Build requirements:
#   • sdbusplus (for the type declarations — no daemon needed at test time)
#   • Boost.Test (header-only, included via boost_dep)
#   • Boost.Asio (for io_context, co_spawn, steady_timer)

if not sdbusplus_dep.found()
    warning('sdbusplus not found — skipping dbusproperty_watcher tests')
    subdir_done()
endif

cxx = meson.get_compiler('cpp')

test_dbus_watcher = executable(
    'test_dbusproperty_watcher',
    'test_dbusproperty_watcher.cpp',
    include_directories: reactor_inc,
    dependencies: [
        boost_dep,
        sdbusplus_dep,
    ],
    cpp_args: [
        '-DHAVE_SDBUSPLUS',
        '-DBOOST_TEST_DYN_LINK=0',   # header-only Boost.Test
        '-DBOOST_ASIO_DISABLE_THREADS',
        '-DBOOST_ALL_NO_LIB',
    ],
    build_by_default: false,
)

test(
    'dbusproperty_watcher',
    test_dbus_watcher,
    timeout: 60,
)

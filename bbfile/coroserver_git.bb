SUMMARY = "Coroserver: A sample server using Meson build system"
DESCRIPTION = "Coroserver is a sample server project that uses the Meson build system."
HOMEPAGE = "https://github.com/abhilashraju/coroserver"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=c87d289892671dc3843f34743e535818"
DEPENDS = " \
    boost \
    gtest \
    libpam \
    nlohmann-json \
    openssl \
    sdbusplus \
"

SRC_URI = "git://github.com/abhilashraju/coroserver.git;branch=main;protocol=https"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

inherit systemd
inherit pkgconfig meson

EXTRA_OEMESON = " \
    --buildtype=minsize \
"

# Specify the source directory
S = "${WORKDIR}/git"

# Specify the installation directory
bindir = "/usr/bin"
plugindir = "/usr/lib/plugins"
sysconfdir = "/etc/ssl/private"

do_install() {
    install -d ${D}${bindir}
    #install -d ${D}${plugindir}
    install -m 0755 ${B}/examples/dbus_inspector/dbus_inspector ${D}${bindir}/dbus_inspector
    #install -m 0755 ${B}/examples/event_broker/event_broker ${D}${bindir}/event_broker
    #install -m 0755 ${B}/examples/event_broker/plugins/libevent_broker_plugin.so ${D}${plugindir}/libevent_broker_plugin.so
    #install -m 0755 ${B}/examples/event_broker/publisher/event_publisher ${D}${bindir}/event_publisher

    install -d ${D}${sysconfdir}
    install -m 0644 ${S}/examples/dbus_inspector/server-cert.pem ${D}${sysconfdir}/server-cert.pem
    install -m 0644 ${S}/examples/dbus_inspector/server-key.pem ${D}${sysconfdir}/server-key.pem
}

# Specify the package information
FILES_${PN} = "${bindir}/* ${sysconfdir}/*"
FILES_${PN} += "${plugindir}/*"

# Create a separate package for the plugin
PACKAGES += "${PN}-plugin"
FILES_${PN}-plugin = "${plugindir}/libevent_broker_plugin.so"

# Ensure the .so file is not included in the -dev package
FILES_${PN}-dev = ""

# Suppress the dev-elf QA issue
INSANE_SKIP_${PN} = "dev-elf"
INSANE_SKIP_${PN}-plugin = "dev-elf"

# Enable wrap-based subproject downloading
#EXTRA_OEMESON += "-Dwrap_mode=forcefallback"
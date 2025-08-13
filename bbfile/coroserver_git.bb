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
    libarchive \
    libpam \
    systemd \
    sdeventplus \
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
syscadir = "/etc/ssl/certs"
syscertdir = "/etc/ssl/certs/https"
sysprivatedir = "/etc/ssl/private"
systemd_system_unitdir = "/etc/systemd/system"
varconfdir = "/var/spdm"
etc_dbus_conf = "/etc/dbus-1/system.d"

do_install() {
     install -d ${D}${bindir}
     #install -d ${D}${plugindir}
     #install -m 0755 ${B}/examples/dbus_inspector/dbus_inspector ${D}${bindir}/dbus_inspector
     install -m 0755 ${B}/examples/event_broker/event_broker ${D}${bindir}/event_broker
     #install -m 0755 ${B}/examples/event_broker/plugins/libevent_broker_plugin.so ${D}${plugindir}/libevent_broker_plugin.so
     install -m 0755 ${B}/examples/event_broker/publisher/event_publisher ${D}${bindir}/event_publisher
     install -m 0755 ${B}/examples/journalserver/journalserver ${D}${bindir}/journalserver
     install -m 0755 ${B}/examples/spdmlite/spdmlite ${D}${bindir}/spdmlite
     install -m 0755 ${B}/examples/redfish_client/redfish_client ${D}${bindir}/redfish_client
     install -m 0755 ${B}/examples/redfish_listener/redfish_listener ${D}${bindir}/redfish_listener
     install -m 0755 ${B}/examples/mctp_requester/mctp_requester ${D}${bindir}/mctp_requester
     install -m 0755 ${B}/examples/mctp_responder/mctp_responder ${D}${bindir}/mctp_responder

     install -d ${D}${syscadir}
     install -d ${D}${syscertdir}
     install -d ${D}${sysprivatedir}
     install -m 0644 ${S}/examples/spdmlite/certs/client_cert.pem ${D}${syscertdir}/client_cert.pem
     install -m 0644 ${S}/examples/spdmlite/certs/server_cert.pem ${D}${syscertdir}/server_cert.pem
     install -m 0644 ${S}/examples/spdmlite/certs/ca.pem ${D}${syscadir}/ca.pem
   
     install -m 0644 ${S}/examples/spdmlite/certs/server_key.pem ${D}${sysprivatedir}/server_key.pem
     install -m 0644 ${S}/examples/spdmlite/certs/client_key.pem ${D}${sysprivatedir}/client_key.pem
     install -m 0644 ${S}/examples/spdmlite/certs/ca_key.pem ${D}${sysprivatedir}/ca_key.pem


     install -d ${D}${systemd_system_unitdir}
     install -d ${D}${etc_dbus_conf}
     install -d ${D}${varconfdir}
     
     install -m 0644 ${S}/examples/spdmlite/service/xyz.openbmc_project.spdmlite.service ${D}${systemd_system_unitdir}/
     install -m 0644 ${S}/examples/spdmlite/spdm.conf ${D}${varconfdir}/
     install -m 0644 ${S}/examples/spdmlite/service/xyz.openbmc_project.spdmlite.conf ${D}${etc_dbus_conf}/
}

FILES:${PN} += "/usr/bin/journalserver"
FILES:${PN} += "/usr/bin/redfish_listener"
FILES:${PN} += "/usr/bin/event_broker"
FILES:${PN} += "/usr/bin/redfish_client"
FILES:${PN} += "/usr/bin/spdmlite"
FILES:${PN} += "/usr/bin/event_publisher"
FILES:${PN} += "/usr/bin/mctp_requester"
FILES:${PN} += "/usr/bin/mctp_responder"
FILES:${PN} += "/etc/systemd/system/xyz.openbmc_project.spdmlite.service"
FILES:${PN} += "/var/spdm/spdm.conf"
FILES:${PN} += "/etc/dbus-1/system.d/xyz.openbmc_project.spdmlite.conf"


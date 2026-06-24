SUMMARY = "coroserver example applications"
DESCRIPTION = "Build and package installable example applications from the coroserver Meson project"
HOMEPAGE = "https://github.com/abhilashraju/coroserver"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=c87d289892671dc3843f34743e535818"

PV = "1.0+git${SRCPV}"
SRC_URI = "git://github.com/abhilashraju/coroserver.git;branch=main;protocol=https"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

inherit meson pkgconfig systemd

DEPENDS = " \
    boost \
    nlohmann-json \
    openssl \
    zlib \
    sdeventplus \
    sdbusplus \
    libssh2 \
"

PACKAGECONFIG ??= "spdm"
PACKAGECONFIG[libgraphqlparser] = ",,libgraphqlparser"
PACKAGECONFIG[spdm] = "-Dspdm=enabled,-Dspdm=disabled,libspdm phosphor-logging phosphor-dbus-interfaces"

PACKAGES =+ " \
    ${PN}-console \
    ${PN}-graphql \
    ${PN}-tcp-server \
    ${PN}-lldp-discoverd \
    ${PN}-redfishproxy \
    ${PN}-i2c-service \
"

PACKAGES =+ "${@bb.utils.contains('PACKAGECONFIG', 'spdm', '${PN}-spdm', '', d)}"

FILES:${PN} = ""
ALLOW_EMPTY:${PN} = "1"

FILES:${PN}-console = " \
    ${bindir}/console_server \
    ${bindir}/console_client \
    ${systemd_system_unitdir}/console_server.service \
    ${systemd_system_unitdir}/obmc-console-ssh@2202.service \
    ${sysconfdir}/obmc-console-multi.conf \
    ${sysconfdir}/obmc-console/obmc-console-ssh-remote.conf \
    ${sysconfdir}/obmc-console/sshd.2202.conf \
"

FILES:${PN}-graphql = " \
    ${bindir}/graphql_server_libgraphql \
"

FILES:${PN}-tcp-server = " \
    ${bindir}/tcp_server \
    ${sysconfdir}/ssl/private/server-cert.pem \
    ${sysconfdir}/ssl/private/server-key.pem \
"

FILES:${PN}-lldp-discoverd = " \
    ${bindir}/lldp_discoverd \
    ${systemd_system_unitdir}/lldp_discoverd.service \
"

FILES:${PN}-redfishproxy = " \
    ${bindir}/redfishproxy \
    ${systemd_system_unitdir}/redfishproxy.service \
"

FILES:${PN}-spdm = " \
    ${bindir}/spdm_responder \
    ${bindir}/spdm_requester \
    ${systemd_system_unitdir}/xyz.openbmc_project.spdm.responder.service \
    ${systemd_system_unitdir}/xyz.openbmc_project.spdm.requester.service \
    ${sysconfdir}/dbus-1/system.d/xyz.openbmc_project.spdm.responder.conf \
    ${sysconfdir}/dbus-1/system.d/xyz.openbmc_project.spdm.requester.conf \
"

FILES:${PN}-i2c-service = " \
    ${bindir}/i2c-service \
    ${systemd_system_unitdir}/com.ibm.I2CService.service \
"

FILES:${PN}-dev = " \
    ${includedir}/reactor \
    ${libdir}/pkgconfig/reactor.pc \
"

SYSTEMD_PACKAGES = "${PN}-lldp-discoverd ${PN}-redfishproxy ${PN}-i2c-service"
SYSTEMD_PACKAGES += "${@bb.utils.contains('PACKAGECONFIG', 'spdm', '${PN}-spdm', '', d)}"

SYSTEMD_SERVICE:${PN}-lldp-discoverd = "lldp_discoverd.service"
SYSTEMD_SERVICE:${PN}-redfishproxy = "redfishproxy.service"
SYSTEMD_SERVICE:${PN}-i2c-service = "com.ibm.I2CService.service"
SYSTEMD_SERVICE:${PN}-spdm = "xyz.openbmc_project.spdm.responder.service xyz.openbmc_project.spdm.requester.service"

RDEPENDS:${PN}-lldp-discoverd += "systemd"
RDEPENDS:${PN}-redfishproxy += "systemd"
RDEPENDS:${PN}-i2c-service += "systemd"
RDEPENDS:${PN}-spdm += "systemd"
#PACKAGECONFIG:remove:pn-coroserver-examples = "spdm"

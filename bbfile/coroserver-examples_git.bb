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
"

PACKAGECONFIG ??= ""
PACKAGECONFIG[libgraphqlparser] = ",,libgraphqlparser"

PACKAGES =+ " \
    ${PN}-console \
    ${PN}-graphql \
    ${PN}-tcp-server \
    ${PN}-lldp-discoverd \
"

FILES:${PN} = ""
ALLOW_EMPTY:${PN} = "1"

FILES:${PN}-console = " \
    ${bindir}/console_server \
    ${bindir}/console_client \
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

FILES:${PN}-dev = " \
    ${includedir}/reactor \
    ${libdir}/pkgconfig/reactor.pc \
"

SYSTEMD_PACKAGES = "${PN}-lldp-discoverd"
SYSTEMD_SERVICE:${PN}-lldp-discoverd = "lldp_discoverd.service"

RDEPENDS:${PN}-lldp-discoverd += "systemd"
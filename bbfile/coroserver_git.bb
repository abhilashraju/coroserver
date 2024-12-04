
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
sysconfdir = "/etc/ssl/private"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/examples/dbus_inspector/dbus_inspector ${D}${bindir}/dbus_inspector

    install -d ${D}${sysconfdir}
    install -m 0644 ${S}/examples/dbus_inspector/server-cert.pem ${D}${sysconfdir}/server-cert.pem
    install -m 0644 ${S}/examples/dbus_inspector/server-key.pem ${D}${sysconfdir}/server-key.pem
}

# Specify the package information
FILES_${PN} = "${bindir}/* ${sysconfdir}/*"

# Enable wrap-based subproject downloading
#EXTRA_OEMESON += "-Dwrap_mode=forcefallback"



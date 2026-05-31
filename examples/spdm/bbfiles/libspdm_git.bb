SUMMARY = "libspdm - Reference implementation of the DMTF SPDM protocol"
HOMEPAGE = "https://github.com/DMTF/libspdm"
LICENSE = "BSD-3-Clause"

SRC_URI = "git://github.com/DMTF/libspdm.git;protocol=https;branch=main"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

inherit cmake pkgconfig

S = "${WORKDIR}/git"

# Configure library build options. Adjust as needed for your distro/features.
EXTRA_OECMAKE = "\
    -DBUILD_SHARED_LIBS=ON \
    -DENABLE_TESTING=OFF \
    -DCMAKE_INSTALL_SYSCONFDIR=${sysconfdir} \
"

# Ensure headers and pkgconfig are packaged correctly
FILES_${PN}-dev += "${includedir}/*"
FILES_${PN} += "${libdir}/*"
FILES_${PN}-dbg += "${libdir}/*.debug"

# Small tidy-up: do not run upstream tests during image builds
do_configure[dirs] = "${S}"
do_compile[dirs] = "${S}"
do_install[dirs] = "${S}"
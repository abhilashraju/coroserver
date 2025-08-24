SUMMARY = "reactor: Library for developing asynchornous applications"
DESCRIPTION = "reactor is header only library to develop asynchornous io application using coroutine"
HOMEPAGE = "https://github.com/abhilashraju/coroserver"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=c87d289892671dc3843f34743e535818"

SRC_URI = "git://github.com/abhilashraju/coroserver.git;branch=main;protocol=https"
SRCREV = "${AUTOREV}"
S = "${WORKDIR}/git"            

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    
    install -d ${D}${includedir}/reactor

    cp -r ${S}/include/* ${D}${includedir}/reactor/

    install -d ${D}${libdir}/pkgconfig
    cp ${S}/pkgconfig/reactor.pc ${D}${libdir}/pkgconfig/reactor.pc
}

        
FILES:${PN} += "${includedir}/reactor/*"
FILES_${PN}-dev += "${libdir}/pkgconfig/reactor.pc"




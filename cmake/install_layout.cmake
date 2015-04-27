# Set the install layout
# Possible values:
# STANDALONE - Installs to /usr/local/mariadb-maxscale
# RPM - Installs to /usr
# DEB - Installs to /usr
include(GNUInstallDirs)

set(MAXSCALE_LIBDIR ${CMAKE_INSTALL_LIBDIR}/maxscale CACHE PATH "Library installation path")
set(MAXSCALE_BINDIR ${CMAKE_INSTALL_BINDIR} CACHE PATH "Executable installation path")
set(MAXSCALE_SHAREDIR ${CMAKE_INSTALL_DATADIR}/maxscale CACHE PATH "Share file installation path, includes licence and readme files")
set(MAXSCALE_DOCDIR ${CMAKE_INSTALL_DOCDIR}/maxscale CACHE PATH "Documentation installation path, text versions only")
set(MAXSCALE_CONFDIR ${CMAKE_INSTALL_SYSCONFDIR} CACHE PATH "Configuration file installation path, this is not usually needed")
set(MAXSCALE_VARDIR /var CACHE PATH "Data file path (usually /var/)")


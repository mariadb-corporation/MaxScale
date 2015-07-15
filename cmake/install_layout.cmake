# Set the install layout
include(GNUInstallDirs)

set(MAXSCALE_LIBDIR ${CMAKE_INSTALL_LIBDIR}/maxscale CACHE PATH "Library installation path")
set(MAXSCALE_BINDIR ${CMAKE_INSTALL_BINDIR} CACHE PATH "Executable installation path")
set(MAXSCALE_SHAREDIR ${CMAKE_INSTALL_DATADIR}/maxscale CACHE PATH "Share file installation path, includes licence and readme files")
set(MAXSCALE_DOCDIR ${CMAKE_INSTALL_DOCDIR}/maxscale CACHE PATH "Documentation installation path, text versions only")

# These are the only hard-coded absolute paths
set(MAXSCALE_VARDIR /var CACHE PATH "Data file path (usually /var/)")
set(MAXSCALE_CONFDIR /etc CACHE PATH "Configuration file installation path (/etc/)")

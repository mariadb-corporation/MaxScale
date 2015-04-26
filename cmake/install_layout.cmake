# Set the install layout
# Possible values:
# STANDALONE - Installs to /usr/local/mariadb-maxscale
# RPM - Installs to /usr
# DEB - Installs to /usr
if(${TYPE} MATCHES "STANDALONE")

  set(CMAKE_INSTALL_PREFIX "/usr/local/mariadb-maxscale" CACHE PATH "Prefix prepended to install directories.")

  # RPM and DEB are the same until differences are found
else()
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Prefix prepended to install directories.")
endif()

set(MAXSCALE_LIBDIR lib64/maxscale CACHE PATH "Library installation path")
set(MAXSCALE_BINDIR bin CACHE PATH "Executable installation path")
set(MAXSCALE_SHAREDIR share/maxscale CACHE PATH "Share file installation path, includes licence and readme files")
set(MAXSCALE_DOCDIR share/doc/maxscale CACHE PATH "Documentation installation path, text versions only")
set(MAXSCALE_CONFDIR etc CACHE PATH "Configuration file installation path, this is not usually needed")


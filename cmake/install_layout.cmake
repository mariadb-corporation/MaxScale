# Set the install layout
# Possible values:
# STANDALONE - Installs to /usr/local/mariadb-maxscale
# RPM - Installs to /usr
# DEB - Installs to /usr
function(set_install_layout TYPE)

  if(${TYPE} MATCHES "STANDALONE")

    set(CMAKE_INSTALL_PREFIX "/usr/local/mariadb-maxscale" CACHE PATH "Prefix prepended to install directories.")

# RPM and DEB are the same until differences are found
  elseif(${TYPE} MATCHES "RPM")

    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Prefix prepended to install directories.")

  elseif(${TYPE} MATCHES "DEB")

    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Prefix prepended to install directories.")

  endif()

    set(MAXSCALE_MODULE_INSTALL lib64/maxscale CACHE PATH "Module installation path")
    set(MAXSCALE_LIBRARY_INSTALL lib64/maxscale CACHE PATH "Library installation path")
    set(MAXSCALE_EXECUTABLE_INSTALL bin CACHE PATH "Executable installation path")
    set(MAXSCALE_SHARE_DIR share/maxscale CACHE PATH "Share file installation path, includes licence and readme files")
    set(MAXSCALE_DOC_DIR ${MAXSCALE_SHARE_DIR}/doc CACHE PATH "Documentation installation path, text versions only")
    set(MAXSCALE_CONFIG_DIR ${MAXSCALE_SHARE_DIR}/etc CACHE PATH "Configuration file installation path, example configurations will be placed here")
endfunction()

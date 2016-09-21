# Set the install layout
include(GNUInstallDirs)

set(MAXSCALE_LIBDIR ${CMAKE_INSTALL_LIBDIR}/maxscale CACHE PATH "Library installation path")
set(MAXSCALE_BINDIR ${CMAKE_INSTALL_BINDIR} CACHE PATH "Executable installation path")
set(MAXSCALE_SHAREDIR ${CMAKE_INSTALL_DATADIR}/maxscale CACHE PATH "Share file installation path, includes licence and readme files")
set(MAXSCALE_DOCDIR ${CMAKE_INSTALL_DOCDIR}/maxscale CACHE PATH "Documentation installation path, text versions only")

# These are the only hard-coded absolute paths
set(MAXSCALE_VARDIR /var CACHE PATH "Data file path (usually /var/)")
set(MAXSCALE_CONFDIR /etc CACHE PATH "Configuration file installation path (/etc/)")

# Default values for directories and subpaths where files are searched. These
# are used in `server/include/gwdirs.h.in`.

set(DEFAULT_PID_SUBPATH "run/maxscale" CACHE PATH "Default PID file subpath")
set(DEFAULT_LOG_SUBPATH "log/maxscale" CACHE PATH "Default log subpath")
set(DEFAULT_DATA_SUBPATH "lib/maxscale" CACHE PATH "Default datadir subpath")
set(DEFAULT_LIB_SUBPATH "${MAXSCALE_LIBDIR}" CACHE PATH "Default library subpath")
set(DEFAULT_CACHE_SUBPATH "cache/maxscale" CACHE PATH "Default cache subpath")
set(DEFAULT_LANG_SUBPATH "lib/maxscale" CACHE PATH "Default language file subpath")
set(DEFAULT_EXEC_SUBPATH "${MAXSCALE_BINDIR}" CACHE PATH "Default executable subpath")
set(DEFAULT_CONFIG_SUBPATH "etc" CACHE PATH "Default configuration subpath")

set(DEFAULT_PIDDIR ${MAXSCALE_VARDIR}/${DEFAULT_PID_SUBPATH} CACHE PATH "Default PID file directory")
set(DEFAULT_LOGDIR ${MAXSCALE_VARDIR}/${DEFAULT_LOG_SUBPATH} CACHE PATH "Default log directory")
set(DEFAULT_DATADIR ${MAXSCALE_VARDIR}/${DEFAULT_DATA_SUBPATH} CACHE PATH "Default datadir path")
set(DEFAULT_LIBDIR ${CMAKE_INSTALL_PREFIX}/${DEFAULT_LIB_SUBPATH}/ CACHE PATH "Default library path")
set(DEFAULT_CACHEDIR ${MAXSCALE_VARDIR}/${DEFAULT_CACHE_SUBPATH} CACHE PATH "Default cache directory")
set(DEFAULT_LANGDIR ${MAXSCALE_VARDIR}/${DEFAULT_LANG_SUBPATH} CACHE PATH "Default language file directory")
set(DEFAULT_EXECDIR ${CMAKE_INSTALL_PREFIX}/${DEFAULT_EXEC_SUBPATH} CACHE PATH "Default executable directory")
set(DEFAULT_CONFIGDIR /${DEFAULT_CONFIG_SUBPATH} CACHE PATH "Default configuration directory")

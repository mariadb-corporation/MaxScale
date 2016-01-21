
function(debugmsg MSG)
  if(DEBUG_OUTPUT)
	message(STATUS "DEBUG: ${MSG}")
  endif()
endfunction()

macro(set_maxscale_version)

  # MaxScale version number
  set(MAXSCALE_VERSION_MAJOR "1" CACHE STRING "Major version")
  set(MAXSCALE_VERSION_MINOR "4" CACHE STRING "Minor version")
  set(MAXSCALE_VERSION_PATCH "0" CACHE STRING "Patch version")
  set(MAXSCALE_VERSION_NUMERIC "${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}.${MAXSCALE_VERSION_PATCH}")
  set(MAXSCALE_VERSION "beta-${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}.${MAXSCALE_VERSION_PATCH}")

  # This should be incremented each time a package is rebuilt
  set(MAXSCALE_BUILD_NUMBER 1 CACHE STRING "Release number")
endmacro()

macro(set_variables)

  # Use C99
  set(USE_C99 TRUE CACHE BOOL "Use C99 standard")

  # Install the template maxscale.cnf file
  set(WITH_MAXSCALE_CNF TRUE CACHE BOOL "Install the template maxscale.cnf file")

  # hostname or IP address of MaxScale's host
  set(TEST_HOST "127.0.0.1" CACHE STRING "hostname or IP address of MaxScale's host")

  # port of read connection router module
  set(TEST_PORT "4008" CACHE STRING "port of read connection router module")

  # port of read/write split router module
  set(TEST_PORT_RW "4006" CACHE STRING "port of read/write split router module")

  # port of schemarouter router module
  set(TEST_PORT_DB "4010" CACHE STRING "port of schemarouter router module")

  # port of read/write split router module with hints
  set(TEST_PORT_RW_HINT "4009" CACHE STRING "port of read/write split router module with hints")

  # master test server server_id
  set(TEST_MASTER_ID "3000" CACHE STRING "master test server server_id")

  # master test server port
  set(MASTER_PORT "3000" CACHE STRING "master test server port")

  # username of MaxScale user
  set(TEST_USER "maxuser" CACHE STRING "username of MaxScale user")

  # password of MaxScale user
  set(TEST_PASSWORD "maxpwd" CACHE STRING "password of MaxScale user")

  # Use static version of libmysqld
  set(STATIC_EMBEDDED TRUE CACHE BOOL "Use static version of libmysqld")

  # Build RabbitMQ components
  set(BUILD_RABBITMQ TRUE CACHE BOOL "Build RabbitMQ components")

  # Build the binlog router
  set(BUILD_BINLOG TRUE CACHE BOOL "Build binlog router")

  # Build the multimaster monitor
  set(BUILD_MMMON TRUE CACHE BOOL "Build multimaster monitor")

  # Build Luafilter
  set(BUILD_LUAFILTER FALSE CACHE BOOL "Build Luafilter")

  # Use gcov build flags
  set(GCOV FALSE CACHE BOOL "Use gcov build flags")

  # Install init.d scripts and ldconf configuration files
  set(WITH_SCRIPTS TRUE CACHE BOOL "Install init.d scripts and ldconf configuration files")

  # Use tcmalloc as the memory allocator
  set(WITH_TCMALLOC FALSE CACHE BOOL "Use tcmalloc as the memory allocator")

  # Use jemalloc as the memory allocator
  set(WITH_JEMALLOC FALSE CACHE BOOL "Use jemalloc as the memory allocator")

  # Build tests
  set(BUILD_TESTS FALSE CACHE BOOL "Build tests")

  # Build packages
  set(PACKAGE FALSE CACHE BOOL "Enable package building (this disables local installation of system files)")

  # Build extra tools
  set(BUILD_TOOLS FALSE CACHE BOOL "Build extra utility tools")

  # Profiling
  set(PROFILE FALSE CACHE BOOL "Profiling (gprof)")

endmacro()

macro(check_deps)


  # Check for libraries MaxScale depends on
  find_library(HAVE_LIBAIO NAMES aio)
  if(NOT HAVE_LIBAIO)
    message(FATAL_ERROR "Could not find libaio")
  endif()

  find_library(HAVE_LIBSSL NAMES ssl)
  if(NOT HAVE_LIBSSL)
    message(FATAL_ERROR "Could not find libssl")
  endif()

  find_library(HAVE_LIBCRYPT NAMES crypt)
  if(NOT HAVE_LIBCRYPT)
    message(FATAL_ERROR "Could not find libcrypt")
  endif()

  find_library(HAVE_LIBCRYPTO NAMES crypto)
  if(NOT HAVE_LIBCRYPTO)
    message(FATAL_ERROR "Could not find libcrypto")
  endif()

  find_library(HAVE_LIBZ NAMES z)
  if(NOT HAVE_LIBZ)
    message(FATAL_ERROR "Could not find libz")
  endif()

  find_library(HAVE_LIBM NAMES m)
  if(NOT HAVE_LIBM)
    message(FATAL_ERROR "Could not find libm")
  endif()

  find_library(HAVE_LIBDL NAMES dl)
  if(NOT HAVE_LIBDL)
    message(FATAL_ERROR "Could not find libdl")
  endif()

  find_library(HAVE_LIBRT NAMES rt)
  if(NOT HAVE_LIBRT)
    message(FATAL_ERROR "Could not find librt")
  endif()

  find_library(HAVE_LIBPTHREAD NAMES pthread)
  if(NOT HAVE_LIBPTHREAD)
    message(FATAL_ERROR "Could not find libpthread")
  endif()


endmacro()

macro(check_dirs)
  # Check which init.d script to install
  if(WITH_SCRIPTS)
    find_file(RPM_FNC functions PATHS /etc/rc.d/init.d)
    if(${RPM_FNC} MATCHES "RPM_FNC-NOTFOUND")
      find_file(DEB_FNC init-functions PATHS /lib/lsb)
      if(${DEB_FNC} MATCHES "DEB_FNC-NOTFOUND")
	    message(FATAL_ERROR "Cannot find required init-functions in /lib/lsb/ or /etc/rc.d/init.d/, please confirm that your system files are OK.")
      else()
	    set(DEB_BASED TRUE CACHE BOOL "If init.d script uses /lib/lsb/init-functions instead of /etc/rc.d/init.d/functions.")
      endif()
    else()
      set(DEB_BASED FALSE CACHE BOOL "If init.d script uses /lib/lsb/init-functions instead of /etc/rc.d/init.d/functions.")
    endif()
    unset(DEB_FNC)
    unset(RPM_FNC)
  endif()

  #Check RabbitMQ headers and libraries
  if(BUILD_RABBITMQ)
	find_package(RabbitMQ)
  endif()

endmacro()

function(subdirs VAR DIRPATH)

  if(${CMAKE_VERSION} VERSION_LESS 2.8.12 )
    set(COMP_VAR PATH)
  else()
    set(COMP_VAR DIRECTORY)
  endif()
  file(GLOB_RECURSE SDIR ${DIRPATH}/*)
  foreach(LOOP ${SDIR})
	get_filename_component(LOOP ${LOOP} ${COMP_VAR})
	list(APPEND ALLDIRS ${LOOP})
  endforeach()
  list(REMOVE_DUPLICATES ALLDIRS)
  set(${VAR} "${ALLDIRS}" CACHE PATH " " FORCE)

endfunction()

function(debugmsg MSG)
  if(DEBUG_OUTPUT)
	message(STATUS "DEBUG: ${MSG}")
  endif()
endfunction()

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

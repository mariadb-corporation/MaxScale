macro(set_maxscale_version)

  #MaxScale version number
  set(MAXSCALE_VERSION_MAJOR "1")
  set(MAXSCALE_VERSION_MINOR "0")
  set(MAXSCALE_VERSION_PATCH "0")
  set(MAXSCALE_VERSION "${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}.${MAXSCALE_VERSION_PATCH}-beta")

endmacro()

macro(set_variables)

  # Installation directory
  set(INSTALL_DIR "/usr/local/skysql/maxscale/" CACHE PATH "MaxScale installation directory.")
  
  # Build type
  set(BUILD_TYPE "Release" CACHE STRING "Build type, possible values are:None (no optimization), Debug, Release.")
  
  # hostname or IP address of MaxScale's host
  set(TEST_HOST "127.0.0.1" CACHE STRING "hostname or IP address of MaxScale's host")

  # port of read connection router module
  set(TEST_PORT_RW "4008" CACHE STRING "port of read connection router module")

  # port of read/write split router module
  set(TEST_PORT_RW "4006" CACHE STRING "port of read/write split router module")

  # port of read/write split router module with hints
  set(TEST_PORT_RW_HINT "4006" CACHE STRING "port of read/write split router module with hints")

  # master test server server_id
  set(TEST_MASTER_ID "3000" CACHE STRING "master test server server_id")

  # username of MaxScale user
  set(TEST_USER "maxuser" CACHE STRING "username of MaxScale user")

  # password of MaxScale user
  set(TEST_PASSWORD "maxpwd" CACHE STRING "password of MaxScale user")
  
  # Use static version of libmysqld
  set(STATIC_EMBEDDED TRUE CACHE BOOL "Use static version of libmysqld")
  
  # Build RabbitMQ components
  set(BUILD_RABBITMQ FALSE CACHE BOOL "Build RabbitMQ components")
  
  # Use gcov build flags
  set(GCOV FALSE CACHE BOOL "Use gcov build flags")

  # Install init.d scripts and ldconf configuration files
  set(INSTALL_SYSTEM_FILES TRUE CACHE BOOL "Install init.d scripts and ldconf configuration files")

  # Build tests
  set(BUILD_TESTS TRUE CACHE BOOL "Build tests")


endmacro()

macro(check_deps)

  # Check for libraries MaxScale depends on
  set(MAXSCALE_DEPS aio ssl crypt crypto z m dl rt pthread)
  foreach(lib ${MAXSCALE_DEPS})
    find_library(lib${lib} ${lib})
    if((DEFINED lib${lib}) AND (${lib${lib}} STREQUAL "lib${lib}-NOTFOUND"))
      set(DEPS_ERROR TRUE)
      set(FAILED_DEPS "${FAILED_DEPS} lib${lib}")
	elseif(DEBUG_OUTPUT)
	  message(STATUS "Library was found at: ${lib${lib}}")
    endif()
  endforeach()

  if(DEPS_ERROR)
    message(FATAL_ERROR "Cannot find dependencies: ${FAILED_DEPS}")
  endif()

endmacro()

macro(check_dirs)
  # Find the MySQL headers if they were not defined
  if(NOT ( DEFINED MYSQL_DIR ) )
	find_path(MYSQL_DIR mysql.h PATH_SUFFIXES mysql mariadb)
	if(${MYSQL_DIR} STREQUAL "MYSQL_DIR-NOTFOUND")
      message(FATAL_ERROR "Fatal Error: MySQL headers were not found.")
	elseif(DEBUG_OUTPUT)
	  message(STATUS "Using MySQL headers found at: ${MYSQL_DIR}")
	endif()
  endif()

  # Find the errmsg.sys file if it was not defied
  if( NOT ( DEFINED ERRMSG ) )
	find_file(ERRMSG errmsg.sys PATHS /usr/share/mysql /usr/local/share/mysql ${CUSTOM_ERRMSG} PATH_SUFFIXES english)
	if(${ERRMSG} STREQUAL "ERRMSG-NOTFOUND")
      message(FATAL_ERROR "Fatal Error: The errmsg.sys file was not found.")
	elseif(DEBUG_OUTPUT)
	  message(STATUS "Using errmsg.sys found at: ${ERRMSG}")
	endif()
  endif()

  # Find the embedded mysql library
  if(STATIC_EMBEDDED)
	set(OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
	find_library(EMBEDDED_LIB_STATIC libmysqld.a PATH_SUFFIXES mysql mariadb)      
	set(EMBEDDED_LIB ${EMBEDDED_LIB_STATIC})      
	set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_SUFFIXES})
  else()      
	
	find_library(EMBEDDED_LIB_DYNAMIC mysqld PATH_SUFFIXES mysql mariadb)            
	set(EMBEDDED_LIB ${EMBEDDED_LIB_DYNAMIC})      

  endif()


  # Inform the user about the embedded library
  if( (${EMBEDDED_LIB} STREQUAL "EMBEDDED_LIB_STATIC-NOTFOUND") OR (${EMBEDDED_LIB} STREQUAL "EMBEDDED_LIB_DYNAMIC-NOTFOUND"))
	message(FATAL_ERROR "Library not found: libmysqld. If your install of MySQL is in a non-default location, please provide the location with -DEMBEDDED_LIB=<path to library>")
  else()
	get_filename_component(EMBEDDED_LIB ${EMBEDDED_LIB} REALPATH)
	message(STATUS "Using embedded library: ${EMBEDDED_LIB}")
  endif()


  # Check which init.d script to install
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
  

  set(DEPS_OK TRUE CACHE BOOL "If all the dependencies were found.")

endmacro()

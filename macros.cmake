macro(set_maxscale_version)

  #MaxScale version number
  set(MAXSCALE_VERSION_MAJOR "1")
  set(MAXSCALE_VERSION_MINOR "0")
  set(MAXSCALE_VERSION_PATCH "1")
  set(MAXSCALE_VERSION_NUMERIC "${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}.${MAXSCALE_VERSION_PATCH}")
  set(MAXSCALE_VERSION "${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}.${MAXSCALE_VERSION_PATCH}-beta")

endmacro()

macro(set_variables)

  # Installation directory
  set(INSTALL_DIR "/usr/local/skysql/maxscale/" CACHE PATH "MaxScale installation directory.")
  
  # Build type
  set(BUILD_TYPE "None" CACHE STRING "Build type, possible values are:None, Debug, Optimized.")
  
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
	set(DEPS_OK FALSE CACHE BOOL "If all the dependencies were found.")
    message(FATAL_ERROR "Cannot find dependencies: ${FAILED_DEPS}")
  endif()

endmacro()

macro(check_dirs)

  # This variable is used to prevent redundant checking of dependencies
  set(DEPS_OK TRUE CACHE BOOL "If all the dependencies were found.")

  # Find the MySQL headers if they were not defined
  if(DEFINED MYSQL_DIR)
	if(DEBUG_OUTPUT)
	  message(STATUS "Searching for MySQL headers at: ${MYSQL_DIR}")
	endif()
	find_path(MYSQL_DIR_LOC mysql.h PATHS ${MYSQL_DIR} PATH_SUFFIXES mysql mariadb NO_DEFAULT_PATH)
  endif()
  find_path(MYSQL_DIR_LOC mysql.h PATH_SUFFIXES mysql mariadb)
  if(DEBUG_OUTPUT)
	message(STATUS "Search returned: ${MYSQL_DIR_LOC}")
  endif()
  if(${MYSQL_DIR_LOC} STREQUAL "MYSQL_DIR_LOC-NOTFOUND")
	set(DEPS_OK FALSE CACHE BOOL "If all the dependencies were found.")
    message(FATAL_ERROR "Fatal Error: MySQL headers were not found.")
  else()
	message(STATUS "Using MySQL headers found at: ${MYSQL_DIR}")
	set(MYSQL_DIR ${MYSQL_DIR_LOC} CACHE PATH "Path to MySQL headers" FORCE)
  endif()
  set(MYSQL_DIR_LOC "" INTERNAL)

  # Find the errmsg.sys file if it was not defied
  if( DEFINED ERRMSG )
	find_file(ERRMSG_FILE errmsg.sys PATHS ${ERRMSG} NO_DEFAULT_PATH)
  endif()
  find_file(ERRMSG_FILE errmsg.sys PATHS /usr/share/mysql /usr/local/share/mysql PATH_SUFFIXES english)
  if(${ERRMSG_FILE} MATCHES "ERRMSG_FILE-NOTFOUND")
	set(DEPS_OK FALSE CACHE BOOL "If all the dependencies were found.")
    message(FATAL_ERROR "Fatal Error: The errmsg.sys file was not found, please define the path to it by using -DERRMSG=<path>")
  else()
	message(STATUS "Using errmsg.sys found at: ${ERRMSG_FILE}")
  endif()
  set(ERRMSG ${ERRMSG_FILE} CACHE FILEPATH "Path to the errmsg.sys file." FORCE)
  set(ERRMSG_FILE "" INTERNAL)

  # Find the embedded mysql library
  if(STATIC_EMBEDDED)

	set(OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
	if (DEFINED EMBEDDED_LIB)
	  if(DEBUG_OUTPUT)
		message(STATUS "Searching for libmysqld.a at: ${EMBEDDED_LIB}")
	  endif()
	  find_library(EMBEDDED_LIB_STATIC libmysqld.a PATHS ${EMBEDDED_LIB} PATH_SUFFIXES mysql mariadb NO_DEFAULT_PATH)
	else()
	  find_library(EMBEDDED_LIB_STATIC libmysqld.a PATH_SUFFIXES mysql mariadb)      
	endif()
	if(DEBUG_OUTPUT)
	  message(STATUS "Search returned: ${EMBEDDED_LIB_STATIC}")
	endif()
	set(EMBEDDED_LIB ${EMBEDDED_LIB_STATIC} CACHE FILEPATH "Path to libmysqld" FORCE)      
	set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_SUFFIXES})
	set(OLD_SUFFIXES "" INTERNAL)

  else()      
	if (DEFINED EMBEDDED_LIB)
	  if(DEBUG_OUTPUT)
		message(STATUS "Searching for libmysqld.so at: ${EMBEDDED_LIB}")
	  endif()
	  find_library(EMBEDDED_LIB_DYNAMIC mysqld PATHS ${EMBEDDED_LIB} PATH_SUFFIXES mysql mariadb NO_DEFAULT_PATH) 
	else()
	  find_library(EMBEDDED_LIB_DYNAMIC mysqld PATH_SUFFIXES mysql mariadb)            
	endif()
	if(DEBUG_OUTPUT)
	  message(STATUS "Search returned: ${EMBEDDED_LIB_DYNAMIC}")
	endif()
	set(EMBEDDED_LIB ${EMBEDDED_LIB_DYNAMIC} CACHE FILEPATH "Path to libmysqld" FORCE)      

  endif()
  set(EMBEDDED_LIB_DYNAMIC "" INTERNAL)
  set(EMBEDDED_LIB_STATIC "" INTERNAL)

  # Inform the user about the embedded library
  if( (${EMBEDDED_LIB} STREQUAL "EMBEDDED_LIB_STATIC-NOTFOUND") OR (${EMBEDDED_LIB} STREQUAL "EMBEDDED_LIB_DYNAMIC-NOTFOUND"))
	set(DEPS_OK FALSE CACHE BOOL "If all the dependencies were found.")
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
	  set(DEPS_OK FALSE CACHE BOOL "If all the dependencies were found.")
	  message(FATAL_ERROR "Cannot find required init-functions in /lib/lsb/ or /etc/rc.d/init.d/, please confirm that your system files are OK.")
	else()
	  set(DEB_BASED TRUE CACHE BOOL "If init.d script uses /lib/lsb/init-functions instead of /etc/rc.d/init.d/functions.")
	endif()
  else()
	set(DEB_BASED FALSE CACHE BOOL "If init.d script uses /lib/lsb/init-functions instead of /etc/rc.d/init.d/functions.")
  endif()
  set(DEB_FNC "" INTERNAL)
  set(RPM_FNC "" INTERNAL)

  #Check RabbitMQ headers and libraries
  if(BUILD_RABBITMQ)

	if(DEFINED RABBITMQ_LIB)
	  find_library(RMQ_LIB rabbitmq PATHS ${RABBITMQ_LIB} NO_DEFAULT_PATH)
	endif()
	find_library(RMQ_LIB rabbitmq)
	if(RMQ_LIB STREQUAL "RMQ_LIB-NOTFOUND")
	  set(DEPS_OK FALSE CACHE BOOL "If all the dependencies were found.")
	  message(FATAL_ERROR "Cannot find RabbitMQ libraries, please define the path to the libraries with -DRABBITMQ_LIB=<path>")
	else()
	  set(RABBITMQ_LIB ${RMQ_LIB} CACHE PATH "Path to RabbitMQ libraries" FORCE)
	  message(STATUS "Using RabbitMQ libraries found at: ${RABBITMQ_LIB}")
	endif()

	if(DEFINED RABBITMQ_HEADERS)
	  find_file(RMQ_HEADERS amqp.h PATHS ${RABBITMQ_HEADERS} NO_DEFAULT_PATH)
	endif()
	find_file(RMQ_HEADERS amqp.h)
	if(RMQ_HEADERS STREQUAL "RMQ_HEADERS-NOTFOUND")
	  set(DEPS_OK FALSE CACHE BOOL "If all the dependencies were found.")
	  message(FATAL_ERROR "Cannot find RabbitMQ headers, please define the path to the headers with -DRABBITMQ_HEADERS=<path>")
	else()
	  set(RABBITMQ_HEADERS ${RMQ_HEADERS} CACHE PATH "Path to RabbitMQ headers" FORCE)
	  message(STATUS "Using RabbitMQ headers found at: ${RABBITMQ_HEADERS}")
	endif()

  endif()

endmacro()

function(subdirs VAR DIRPATH)

  file(GLOB_RECURSE SDIR ${DIRPATH}/*)
  foreach(LOOP ${SDIR})
	get_filename_component(LOOP ${LOOP} DIRECTORY)
	list(APPEND ALLDIRS ${LOOP})
  endforeach()
  list(REMOVE_DUPLICATES ALLDIRS)
 set(${VAR} "${ALLDIRS}" CACHE PATH " " FORCE)
endfunction()
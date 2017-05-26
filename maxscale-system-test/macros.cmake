macro(set_maxscale_version)

  #MaxScale-test version number
  set(MAXSCALE_VERSION_MAJOR "1")
  set(MAXSCALE_VERSION_MINOR "3")
  set(MAXSCALE_VERSION_PATCH "0")
  set(MAXSCALE_VERSION_NUMERIC "${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}.${MAXSCALE_VERSION_PATCH}")
  set(MAXSCALE_VERSION "${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}.${MAXSCALE_VERSION_PATCH}-beta")

endmacro()

macro(check_deps)

  find_library(MYSQL_CLIENT mariadbclient mysqlclient PATH_SUFFIXES mysql mariadb)

  # Check for libraries MaxScale depends on
  set(MAXSCALE_DEPS z crypt nsl m pthread ssl crypto dl rt jansson)
  foreach(lib ${MAXSCALE_DEPS})
    find_library(lib${lib} ${lib})
    if((DEFINED lib${lib}) AND (${lib${lib}} MATCHES "NOTFOUND"))
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
  if(DEFINED MYSQL_CLIENT MATCHES "NOTFOUND")
    set(DEPS_OK FALSE CACHE BOOL "If all the dependencies were found.")
    message(FATAL_ERROR "Cannot find dependencies: mariadbclient or mysqlclient")
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
  else()
	find_path(MYSQL_DIR_LOC mysql.h PATH_SUFFIXES mysql mariadb)
  endif()

  if(DEBUG_OUTPUT)
	message(STATUS "Search returned: ${MYSQL_DIR_LOC}")
  endif()

  if(${MYSQL_DIR_LOC} MATCHES "NOTFOUND")
	set(DEPS_OK FALSE CACHE BOOL "If all the dependencies were found.")
    message(FATAL_ERROR "Fatal Error: MySQL headers were not found.")
  else()
	set(MYSQL_DIR ${MYSQL_DIR_LOC} CACHE PATH "Path to MySQL headers" FORCE)
	message(STATUS "Using MySQL headers found at: ${MYSQL_DIR}")
  endif()

  unset(MYSQL_DIR_LOC)

endmacro()


# This CMake file tries to find the the mysql_version.h header
# and to parse it for version and provider strings
# The following variables are set:
# MYSQL_VERSION - The MySQL version number
# MYSQL_PROVIDER - The MySQL provider e.g. MariaDB
# EMBEDDED_LIB - The MySQL embedded library

find_file(MYSQL_VERSION_H mysql_version.h PATH_SUFFIXES mysql)
if(MYSQL_VERSION_H MATCHES "MYSQL_VERSION_H-NOTFOUND")
  message(FATAL_ERROR "Cannot find the mysql_version.h header")
else()
  message(STATUS "Found mysql_version.h: ${MYSQL_VERSION_H}")
endif()


file(READ ${MYSQL_VERSION_H} MYSQL_VERSION_CONTENTS)
string(REGEX REPLACE ".*MYSQL_SERVER_VERSION[^0-9.]+([0-9.]+).*" "\\1" MYSQL_VERSION ${MYSQL_VERSION_CONTENTS})
string(REGEX REPLACE ".*MYSQL_COMPILATION_COMMENT[[:space:]]+\"(.+)\".*" "\\1" MYSQL_PROVIDER ${MYSQL_VERSION_CONTENTS})
string(TOLOWER ${MYSQL_PROVIDER} MYSQL_PROVIDER)
if(MYSQL_PROVIDER MATCHES "[mM]aria[dD][bB]")
  set(MYSQL_PROVIDER "MariaDB" CACHE INTERNAL "The MySQL provider")
elseif(MYSQL_PROVIDER MATCHES "mysql")
  set(MYSQL_PROVIDER "MySQL" CACHE INTERNAL "The MySQL provider")
elseif(MYSQL_PROVIDER MATCHES "percona")
  set(MYSQL_PROVIDER "Percona" CACHE INTERNAL "The MySQL provider")
else()
  set(MYSQL_PROVIDER "Unknown" CACHE INTERNAL "The MySQL provider")
endif()
message(STATUS "MySQL version: ${MYSQL_VERSION}")
message(STATUS "MySQL provider: ${MYSQL_PROVIDER}")

if(NOT MYSQL_PROVIDER STREQUAL "MariaDB")
message(WARNING "Not using a release version of MariaDB server. If this is intentional, please ignore this warning. Otherwise make sure the right libraries are installed and CMake finds the right libraries.")
endif()
if(MYSQL_VERSION VERSION_LESS 5.5.41)
message(WARNING "MySQL version is ${MYSQL_VERSION}. Minimum supported version is 5.5.41.")
endif()

if(NOT (MYSQL_VERSION VERSION_LESS 10.1))

  # 10.1 needs lzma
  find_library(HAVE_LIBLZMA NAMES lzma)
  if(NOT HAVE_LIBLZMA)
    message(FATAL_ERROR "Could not find liblzma")
  endif()
  set(LZMA_LINK_FLAGS "lzma" CACHE STRING "liblzma link flags")
endif()


if (DEFINED EMBEDDED_LIB)
  if( NOT (IS_DIRECTORY ${EMBEDDED_LIB}) )
	debugmsg("EMBEDDED_LIB is not a directory: ${EMBEDDED_LIB}")
	if(${CMAKE_VERSION} VERSION_LESS 2.8.12 )
	  set(COMP_VAR PATH)
	else()
	  set(COMP_VAR DIRECTORY)
	endif()
	get_filename_component(EMBEDDED_LIB ${EMBEDDED_LIB} ${COMP_VAR})
	debugmsg("EMBEDDED_LIB directory component: ${EMBEDDED_LIB}")
  endif()
  debugmsg("Searching for the embedded library at: ${EMBEDDED_LIB}")
endif()

if(STATIC_EMBEDDED)

  debugmsg("Using the static embedded library...")
  set(OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  if (DEFINED EMBEDDED_LIB)
	debugmsg("Searching for libmysqld.a at: ${EMBEDDED_LIB}")
	find_library(EMBEDDED_LIB_STATIC libmysqld.a PATHS ${EMBEDDED_LIB} PATH_SUFFIXES mysql mariadb NO_DEFAULT_PATH)
  else()
	find_library(EMBEDDED_LIB_STATIC libmysqld.a PATH_SUFFIXES mysql mariadb)
  endif()
  debugmsg("Search returned: ${EMBEDDED_LIB_STATIC}")

  set(EMBEDDED_LIB ${EMBEDDED_LIB_STATIC} CACHE FILEPATH "Path to libmysqld" FORCE)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_SUFFIXES})

else()
  debugmsg("Using the dynamic embedded library...")
  set(OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".so")
  if (DEFINED EMBEDDED_LIB)
	debugmsg("Searching for libmysqld.so at: ${EMBEDDED_LIB}")
	find_library(EMBEDDED_LIB_DYNAMIC mysqld PATHS ${EMBEDDED_LIB} PATH_SUFFIXES mysql mariadb NO_DEFAULT_PATH)
  else()
	find_library(EMBEDDED_LIB_DYNAMIC mysqld PATH_SUFFIXES mysql mariadb)
  endif()
  debugmsg("Search returned: ${EMBEDDED_LIB_DYNAMIC}")
  set(EMBEDDED_LIB ${EMBEDDED_LIB_DYNAMIC} CACHE FILEPATH "Path to libmysqld" FORCE)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_SUFFIXES})

endif()

unset(EMBEDDED_LIB_DYNAMIC)
unset(EMBEDDED_LIB_STATIC)
unset(OLD_SUFFIXES)

check_library_exists(${EMBEDDED_LIB} pcre_stack_guard ${EMBEDDED_LIB} HAVE_EMBEDDED_PCRE)

if(HAVE_EMBEDDED_PCRE)
  set(PCRE_LINK_FLAGS "" CACHE INTERNAL "pcre linker flags")
else()
  find_package(PCRE)
  if(PCRE_FOUND)
    set(PCRE_LINK_FLAGS "pcre" CACHE INTERNAL "pcre linker flags")
    message(STATUS "Embedded mysqld does not have pcre_stack_guard, linking with system pcre.")
  else()
    message(FATAL_ERROR "Could not find PCRE libraries.")
  endif()
endif()
if( (${EMBEDDED_LIB} MATCHES "NOTFOUND") OR (${EMBEDDED_LIB} MATCHES "NOTFOUND"))
  message(FATAL_ERROR "Library not found: libmysqld. If your install of MySQL is in a non-default location, please provide the location with -DEMBEDDED_LIB=<path to library>")
else()
  message(STATUS "Using embedded library: ${EMBEDDED_LIB}")
endif()

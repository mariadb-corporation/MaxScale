cmake_minimum_required(VERSION 2.6)

include(macros.cmake)
enable_testing()

set_variables() 
set_maxscale_version()
set(CMAKE_INSTALL_PREFIX "${INSTALL_DIR}" CACHE INTERNAL "Prefix prepended to install directories." FORCE)
set(CMAKE_BUILD_TYPE "${BUILD_TYPE}" CACHE INTERNAL "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel. " FORCE)

project(MaxScale)



set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib:${CMAKE_INSTALL_PREFIX}/modules)

configure_file(${CMAKE_SOURCE_DIR}/server/include/version.h.in ${CMAKE_SOURCE_DIR}/server/include/version.h)
configure_file(${CMAKE_SOURCE_DIR}/maxscale.conf.in ${CMAKE_SOURCE_DIR}/maxscale.conf)
configure_file(${CMAKE_SOURCE_DIR}/etc/init.d/maxscale.in ${CMAKE_SOURCE_DIR}/etc/init.d/maxscale)

if(NOT MYSQL_DIR)
  find_path(MYSQL_DIR mysql.h PATH_SUFFIXES mysql mariadb)
  if(MYSQL_DIR-NOTFOUND)
    message(FATAL_ERROR "Fatal Error: MySQL headers were not found.")
  endif(MYSQL_DIR-NOTFOUND)
endif()

if(NOT ERRMSG)
  find_file(ERRMSG errmsg.sys PATHS /usr/share/mysql /usr/local/share/mysql ${CUSTOM_ERRMSG} PATH_SUFFIXES english)
  if(ERRMSG-NOTFOUND)
    message(FATAL_ERROR "Fatal Error: The errmsg.sys file was not found.")
  endif(ERRMSG-NOTFOUND)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

if(GCOV)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lgcov")
endif()

if((NOT EMBEDDED_LIB) AND STATIC_EMBEDDED)

  find_file(EMBEDDED_LIB libmysqld.a PATHS /usr/lib /usr/lib64 PATH_SUFFIXES mysql mariadb)

  if(EMBEDDED_LIB-NOTFOUND)
    message(WARNING "Warning: Static library not found, looking for dynamic version")
    find_library(EMBEDDED_LIB mysqld PATHS /usr/lib /usr/lib64 PATH_SUFFIXES mysql mariadb)
    get_filename_component(EMBEDDED_LIB ${EMBEDDED_LIB} REALPATH)
  endif()

elseif( NOT STATIC_EMBEDDED)
  
  find_library(EMBEDDED_LIB mysqld PATHS /usr/lib /usr/lib64 PATH_SUFFIXES mysql mariadb)
  get_filename_component(EMBEDDED_LIB ${EMBEDDED_LIB} REALPATH)

endif()

if(EMBEDDED_LIB-NOTFOUND)
  message(FATAL_ERROR "Library not found: libmysqld. If your install of MySQL is in a non-default location, please provide the location with -DEMBEDDED_LIB=<path to library>")
else()
    message(STATUS "Using embedded library: ${EMBEDDED_LIB}")
endif()

include_directories(${MYSQL_DIR})
include_directories(${MYSQL_DIR}/private)
include_directories(${MYSQL_DIR}/extra)
include_directories(utils)
include_directories(log_manager)
include_directories(query_classifier)
include_directories(server/include)
include_directories(server/inih)
include_directories(server/modules/include)

add_subdirectory(utils)
add_subdirectory(log_manager)
add_subdirectory(query_classifier)
add_subdirectory(server)
add_subdirectory(client)

if(BUILD_RABBITMQ)
  message(STATUS "Building RabbitMQ components")
  add_subdirectory(rabbitmq_consumer)
endif(BUILD_RABBITMQ)

file(GLOB DOCS Documentation/*.pdf)

if( NOT ( (DEFINED INSTALL_SYSTEM_FILES) AND ( NOT ( INSTALL_SYSTEM_FILES ) ) ) )
  install(FILES maxscale.conf DESTINATION /etc/ld.so.conf.d/ PERMISSIONS WORLD_EXECUTE WORLD_READ)
  install(FILES etc/init.d/maxscale DESTINATION /etc/init.d/ PERMISSIONS WORLD_EXECUTE)
  message(STATUS "Installing maxscale.conf to: /etc/ld.so.conf.d")
  message(STATUS "Installing startup scripts to: /etc/init.d")
endif()

message(STATUS "Installing MaxScale to: ${CMAKE_INSTALL_PREFIX}/")
install(FILES server/test/MaxScale_test.cnf RENAME MaxScale.cnf DESTINATION etc)
install(FILES ${ERRMSG} DESTINATION mysql)
install(FILES ${EMBEDDED_LIB} DESTINATION lib)
install(FILES ${DOCS} DESTINATION Documentation)

set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MaxScale")
set(CPACK_PACKAGE_NAME "MaxScale")
set(CPACK_PACKAGE_VERSION_MAJOR "${MAXSCALE_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${MAXSCALE_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${MAXSCALE_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "SkySQL Ab")
set(CPACK_PACKAGE_FILE_NAME "maxscale-${MAXSCALE_VERSION}")
set(CPACK_PACKAGE_NAME "maxscale")
set(CPACK_PACKAGE_VENDOR "SkySQL Ab")
set(CPACK_PACKAGE_DESCRIPTION_FILE ${CMAKE_SOURCE_DIR}/README)
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CPACK_RPM_SPEC_INSTALL_POST "/sbin/ldconfig")
set(CPACK_RPM_PACKAGE_NAME "maxscale")
set(CPACK_RPM_PACKAGE_VENDOR "SkySQL Ab")
set(CPACK_RPM_PACKAGE_LICENSE "GPLv2")
set(CPACK_RPM_PACKAGE_AUTOREQPROV " no")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION "/etc /etc/ld.so.conf.d /etc/init.d")
set(CPACK_RPM_USER_FILELIST "%ignore /etc/init.d")
include(CPack)

#add_test(PrepareTests ${CMAKE_COMMAND} -DBUILD_TESTS=Y -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR} -DINSTALL_SYSTEM_FILES=N ${CMAKE_SOURCE_DIR})
#set_tests_properties(PrepareTests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
#set_tests_properties(PrepareTests PROPERTIES COST 1000)

#add_test(BuildTests ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target install)
#set_tests_properties(BuildTests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
#set_tests_properties(BuildTests PROPERTIES DEPENDS PrepareTests)
#set_tests_properties(BuildTests PROPERTIES COST 900)

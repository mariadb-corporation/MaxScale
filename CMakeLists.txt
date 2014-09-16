cmake_minimum_required(VERSION 2.8.12)

include(macros.cmake)


enable_testing()
set_variables() 
set_maxscale_version()

set(CMAKE_INSTALL_PREFIX "${INSTALL_DIR}" CACHE INTERNAL "Prefix prepended to install directories." FORCE)

if(NOT ( BUILD_TYPE STREQUAL "None" ) )
  set(CMAKE_BUILD_TYPE "${BUILD_TYPE}" CACHE INTERNAL "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel. " FORCE)
else()
  set(CMAKE_BUILD_TYPE "")
endif()

project(MaxScale)

if(NOT DEPS_OK)
  check_deps()
else()
  message(STATUS "Dependencies ok")
endif()

check_dirs()

set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib:${CMAKE_INSTALL_PREFIX}/modules)

configure_file(${CMAKE_SOURCE_DIR}/server/include/version.h.in ${CMAKE_SOURCE_DIR}/server/include/version.h)
configure_file(${CMAKE_SOURCE_DIR}/maxscale.conf.in ${CMAKE_SOURCE_DIR}/maxscale.conf)
configure_file(${CMAKE_SOURCE_DIR}/etc/init.d/maxscale.in ${CMAKE_SOURCE_DIR}/etc/init.d/maxscale)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

if(GCOV)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lgcov")
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

if(BUILD_RABBITMQ)
  
  find_path(RABBITMQ_HEADERS amqp.h DOC "Path to the librabbitmq-c headers.")
  find_library(RABBITMQ_LIB rabbitmq DOC "Path to the librabbitmq-c libraries.")
  if(RABBITMQ_HEADERS AND RABBITMQ_LIB)
    message(STATUS "Building RabbitMQ components")
    include_directories(${RABBITMQ_HEADERS})
    link_directories(${RABBITMQ_LIB})
    add_subdirectory(rabbitmq_consumer)
  else()
    message(ERROR "Error: Cannot find the librabbitmq-c headers or libraries.")
  endif()
endif()


add_subdirectory(utils)
add_subdirectory(log_manager)
add_subdirectory(query_classifier)
add_subdirectory(server)
add_subdirectory(client)


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

find_program(RPMBUILD rpmbuild)
find_program(DEBBUILD dpkg-buildpackage)

if(NOT ( ${RPMBUILD} STREQUAL "RPMBUILD-NOTFOUND" ) )
  message(STATUS "Generating RPM packages")
  set(CPACK_GENERATOR "${CPACK_GENERATOR};RPM")
endif()

if(NOT ( ${DEBBUILD} STREQUAL "DEBBUILD-NOTFOUND" ) )
  message(STATUS "Generating DEB packages")
  set(CPACK_GENERATOR "${CPACK_GENERATOR};DEB")
endif()

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MaxScale")
set(CPACK_PACKAGE_VERSION_MAJOR "${MAXSCALE_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${MAXSCALE_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${MAXSCALE_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "SkySQL Ab")
set(CPACK_PACKAGE_FILE_NAME "maxscale-${MAXSCALE_VERSION}")
set(CPACK_PACKAGE_NAME "maxscale")
set(CPACK_PACKAGE_VENDOR "SkySQL Ab")
set(CPACK_PACKAGE_DESCRIPTION_FILE ${CMAKE_SOURCE_DIR}/README)
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "x86_64")
set(CPACK_RPM_SPEC_INSTALL_POST "/sbin/ldconfig")
set(CPACK_RPM_PACKAGE_NAME "maxscale")
set(CPACK_RPM_PACKAGE_VENDOR "SkySQL Ab")
set(CPACK_RPM_PACKAGE_LICENSE "GPLv2")
set(CPACK_RPM_PACKAGE_AUTOREQPROV " no")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION "/etc /etc/ld.so.conf.d /etc/init.d /etc/rc.d/init.d")
set(CPACK_RPM_SPEC_MORE_DEFINE "%define ignore \#")
set(CPACK_RPM_USER_FILELIST "%ignore /etc/init.d")
set(CPACK_RPM_USER_FILELIST "%ignore /etc/ld.so.conf.d")
set(CPACK_RPM_USER_FILELIST "%ignore /etc")
include(CPack)

#add_test(PrepareTests ${CMAKE_COMMAND} -DDEPS_OK=Y -DBUILD_TESTS=Y -DBUILD_TYPE=Debug -DINSTALL_DIR=${CMAKE_BINARY_DIR} -DINSTALL_SYSTEM_FILES=N ${CMAKE_SOURCE_DIR})
#set_tests_properties(PrepareTests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
#set_tests_properties(PrepareTests PROPERTIES COST 2000.0)

#add_test(BuildTests ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target install)
#set_tests_properties(BuildTests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
#set_tests_properties(BuildTests PROPERTIES DEPENDS PrepareTests)
#set_tests_properties(PrepareTests PROPERTIES COST 1999.0)
if(PACKAGE)
  cmake_minimum_required(VERSION 2.8.12)
else()
  cmake_minimum_required(VERSION 2.8)
endif()

message(STATUS "CMake version: ${CMAKE_VERSION}")

include(${CMAKE_SOURCE_DIR}/cmake/macros.cmake)
enable_testing()

# Packaging builds install to /usr and other builds to /usr/local
if(PACKAGE)
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Prefix prepended to install directories.")
endif()

# Set default values for cache entries and set the MaxScale version
include(cmake/defaults.cmake)
include(VERSION22.cmake)
include(ExternalProject)

set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel.")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

project(MaxScale)

# Set the installation layout
include(${CMAKE_SOURCE_DIR}/cmake/install_layout.cmake)

# Do the platform check
include(cmake/CheckPlatform.cmake)

check_dirs()
find_package(OpenSSL)
find_package(Valgrind)
find_package(Pandoc)
find_package(TCMalloc)
find_package(Jemalloc)
find_package(Git)
find_package(RabbitMQ)
find_package(LibUUID)
find_package(Avro)
find_package(GSSAPI)
find_package(SQLite)
find_package(ASAN)

# Build PCRE2 so we always know the version
# Read BuildPCRE2 for details about how to add pcre2 as a dependency to a target
include(cmake/BuildPCRE2.cmake)

include_directories(BEFORE ${PCRE2_INCLUDE_DIRS})

# Always build Connector-C from a known good commit
include(cmake/BuildMariaDBConnector.cmake)
include_directories(BEFORE ${MARIADB_CONNECTOR_INCLUDE_DIR})

include(cmake/BuildJansson.cmake)
include(cmake/BuildMicroHttpd.cmake)

include_directories(${JANSSON_INCLUDE_DIR})

if(NOT OPENSSL_FOUND)
  message(FATAL_ERROR "Failed to locate dependency: OpenSSL")
else()
  if(OPENSSL_VERSION VERSION_LESS 1 AND NOT FORCE_OPENSSL100)
    add_definitions("-DOPENSSL_0_9")
  elseif(OPENSSL_VERSION VERSION_LESS 1.1)
    add_definitions("-DOPENSSL_1_0")
  else()
    add_definitions("-DOPENSSL_1_1")
  endif()
endif()

if(GIT_FOUND)
  message(STATUS "Found git ${GIT_VERSION_STRING}")
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-list --max-count=1 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT
    ERROR_VARIABLE GIT_ERROR
    RESULT_VARIABLE GIT_RVAL)
  if(${GIT_RVAL} EQUAL 0)
    string(REPLACE "\n" "" MAXSCALE_COMMIT ${GIT_COMMIT})
    message(STATUS "Commit ID: ${MAXSCALE_COMMIT}")
  else()
    message(STATUS "Git exited with non-zero value: ${GIT_ERROR}")
    message(STATUS "Could not find repository in source folder, MaxScale commit ID will not be resolved. Will use 'source-build' for commit ID.")
    set(MAXSCALE_COMMIT "source-build")
  endif()
else()
  message(WARNING "Could not find git, MaxScale commit ID will not be resolved. Will use 'source-build' for commit ID.")
  set(MAXSCALE_COMMIT "source-build")
endif()

set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/${MAXSCALE_LIBDIR})

# Make sure the release notes for this release are present if it is a stable one
if(${MAXSCALE_VERSION} MATCHES "-stable")
  file(GLOB ${CMAKE_SOURCE_DIR}/Documentation/Release-Notes RELEASE_NOTES *${MAXSCALE_VERSION_NUMERIC}*.md)
  list(LENGTH RELEASE_NOTES HAVE_NOTES)
  if( NOT HAVE_NOTES EQUAL 1)
    message(FATAL_ERROR "Could not find the release notes for this stable release: ${MAXSCALE_VERSION_NUMERIC}")
  endif()
endif()

# Copy cmake_flags, JENKINS_BUILD_TAG, source and value evironmental variables
# into cmake variables. These are used by the build system to store information
# about the packages being built.
set(MAXSCALE_SOURCE "$ENV{source} $ENV{value}")
set(MAXSCALE_CMAKE_FLAGS "$ENV{cmake_flags}")
set(MAXSCALE_JENKINS_BUILD_TAG "$ENV{BUILD_TAG}")

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/server/include)
configure_file(${CMAKE_SOURCE_DIR}/include/maxscale/version.h.in ${CMAKE_BINARY_DIR}/include/maxscale/version.h @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/include/maxscale/paths.h.in ${CMAKE_BINARY_DIR}/include/maxscale/paths.h @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/include/maxscale/adminusers.h.in ${CMAKE_BINARY_DIR}/include/maxscale/adminusers.h @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/server/test/maxscale_test.h.in ${CMAKE_BINARY_DIR}/include/maxscale/maxscale_test.h @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/etc/postinst.in ${CMAKE_BINARY_DIR}/postinst @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/etc/postrm.in ${CMAKE_BINARY_DIR}/postrm @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/etc/upstart/maxscale.conf.in ${CMAKE_BINARY_DIR}/upstart/maxscale.conf @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/test/maxscale_test.cnf ${CMAKE_BINARY_DIR}/maxscale.cnf @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/test/maxscale_test_secondary.cnf ${CMAKE_BINARY_DIR}/maxscale_secondary.cnf @ONLY)

set(FLAGS "-rdynamic -fno-omit-frame-pointer -Wall -Wno-unused-variable -Wno-unused-function -Werror -fPIC" CACHE STRING "Compilation flags")
set(DEBUG_FLAGS "-ggdb -pthread -pipe -Wformat -fstack-protector --param=ssp-buffer-size=4" CACHE STRING "Debug compilation flags")

if(CMAKE_VERSION VERSION_GREATER 2.6)
  if((CMAKE_C_COMPILER_ID STREQUAL "GNU") AND (NOT (CMAKE_C_COMPILER_VERSION VERSION_LESS 4.2)))
    message(STATUS "C Compiler supports: -Werror=format-security")
    set(DEBUG_FLAGS "${DEBUG_FLAGS} -Werror=format-security" CACHE STRING "Debug compilation flags")
  endif()

  if((CMAKE_C_COMPILER_ID STREQUAL "GNU") AND (NOT (CMAKE_C_COMPILER_VERSION VERSION_LESS 4.6)))
    message(STATUS "C Compiler supports: -Wno-unused-but-set-variable")
    set(FLAGS "${FLAGS} -Wno-unused-but-set-variable " CACHE STRING "Compilation flags")
  endif()
endif()

if(GCOV)
  set(FLAGS "${FLAGS} -fprofile-arcs -ftest-coverage" CACHE STRING "Compilation flags"  FORCE)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lgcov")
endif()

if(PROFILE)
  message(STATUS "Profiling executables")
  set(FLAGS "${FLAGS} -pg " CACHE STRING "Compilation flags" FORCE)
endif()

if(USE_C99)
  message(STATUS "Using C99 standard")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -D_GNU_SOURCE=1 ${FLAGS}")
else()
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAGS}")
endif()

if (WITH_ASAN AND ASAN_FOUND)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
endif()

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${DEBUG_FLAGS} -DSS_DEBUG -DLOG_ASSERT")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wno-uninitialized")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -ggdb -Wno-uninitialized")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAGS} -Wno-deprecated-declarations -std=c++11")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${DEBUG_FLAGS} -DSS_DEBUG -DLOG_ASSERT  -Wno-deprecated-declarations -std=c++11")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wno-deprecated-declarations -Wno-uninitialized -std=c++11")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -ggdb  -Wno-deprecated-declarations -Wno-uninitialized -std=c++11")

include_directories(util)
include_directories(include)
include_directories(server/inih)
include_directories(server/modules/include)
include_directories(${CMAKE_BINARY_DIR}/include)
include_directories(${CURL_INCLUDE_DIRS})

if (BUILD_CDC)
  include_directories(avro)
  add_subdirectory(avro)
endif()

add_subdirectory(plugins)
add_subdirectory(query_classifier)
add_subdirectory(server)
add_subdirectory(include/maxscale)
add_subdirectory(maxctrl)
add_subdirectory(connectors)
if(NOT WITHOUT_MAXADMIN)
  add_subdirectory(client)
endif()
if(BUILD_TESTS)
  add_subdirectory(examples)
endif()

# Generate text versions of some documents
execute_process(COMMAND perl ${CMAKE_SOURCE_DIR}/Documentation/format.pl
${CMAKE_SOURCE_DIR}/Documentation/Changelog.md
${CMAKE_BINARY_DIR}/Changelog.txt)
execute_process(COMMAND perl ${CMAKE_SOURCE_DIR}/Documentation/format.pl
${CMAKE_SOURCE_DIR}/Documentation/Release-Notes/MaxScale-1.2.0-Release-Notes.md
 ${CMAKE_BINARY_DIR}/ReleaseNotes.txt)
execute_process(COMMAND perl ${CMAKE_SOURCE_DIR}/Documentation/format.pl
${CMAKE_SOURCE_DIR}/Documentation/Upgrading/Upgrading-To-MaxScale-1.2.md
 ${CMAKE_BINARY_DIR}/UpgradingToMaxScale12.txt)


install_file(${CMAKE_BINARY_DIR}/Changelog.txt core)
install_file(${CMAKE_BINARY_DIR}/ReleaseNotes.txt core)
install_file(${CMAKE_BINARY_DIR}/UpgradingToMaxScale12.txt core)
install_file(server/maxscale.cnf.template core)
install_file(server/maxscale_binlogserver_template.cnf core)
install_program(script/create_grants core)
install_file(script/create_roles.sql core)

# Install the template into /etc
if(WITH_MAXSCALE_CNF AND (NOT TARGET_COMPONENT OR "core" STREQUAL "${TARGET_COMPONENT}"))
  install_custom_file(server/maxscale.cnf.template ${MAXSCALE_CONFDIR} core)
endif()

install_file(${CMAKE_SOURCE_DIR}/COPYRIGHT core)
install_file(${CMAKE_SOURCE_DIR}/README.md core)
install_file(${CMAKE_SOURCE_DIR}/LICENSE.TXT core)
install_file(${CMAKE_SOURCE_DIR}/LICENSE-THIRDPARTY.TXT core)
install_file(etc/lsyncd_example.conf core)
install_manual(Documentation/maxscale.1 1 core)
install_file(${CMAKE_SOURCE_DIR}/server/maxscale_binlogserver_template.cnf core)

# For devel package, these files are put to the base folder
install_header(${CMAKE_SOURCE_DIR}/COPYRIGHT devel)
install_header(${CMAKE_SOURCE_DIR}/LICENSE.TXT devel)

# Install startup scripts and ldconfig files
if(WITH_SCRIPTS)
  include(cmake/init_scripts.cmake)
endif()

if(PACKAGE)
  # Configure packaging
  include(cmake/package.cmake)

  # Install the files copied by the postinst script into the share folder
  install_program(${CMAKE_BINARY_DIR}/maxscale core)
  install_file(${CMAKE_BINARY_DIR}/maxscale.conf core)
  install_program(${CMAKE_BINARY_DIR}/postinst core)
  install_program(${CMAKE_BINARY_DIR}/postrm core)

  # The inclusion of CPack needs to be the last effective packaging related command. All
  # configurations to packaging done after the call will be ignored.
  include(CPack)
endif()

#
# Custom targets for MaxScale
#

# uninstall target
# see http://www.cmake.org/Wiki/CMake_FAQ#Can_I_do_.22make_uninstall.22_with_CMake.3F
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

find_package(Doxygen)

if(DOXYGEN_FOUND)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/etc/doxygate.in"
    "${CMAKE_CURRENT_BINARY_DIR}/doxygate"
    IMMEDIATE @ONLY)

  add_custom_target(documentation
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/doxygate)
endif()

# Generates PDF documentation
add_custom_target(generate_pdf
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Documentation ${CMAKE_BINARY_DIR}/Documentation
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/Documentation ${CMAKE_COMMAND}
  -DBUILD_DIR=${CMAKE_BINARY_DIR}
  -DCMAKE_MODULE_PATH=${CMAKE_MODULE_PATH}
  -P generate-pdf.cmake
  COMMENT "Generating PDF files" VERBATIM)

# Creates text versions of the release notes
add_custom_target(generate_txt_release
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Documentation ${CMAKE_BINARY_DIR}/Documentation
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/Documentation ${CMAKE_COMMAND}
  -DBUILD_DIR=${CMAKE_BINARY_DIR}
  -DCMAKE_MODULE_PATH=${CMAKE_MODULE_PATH}
  -P generate-txt-release.cmake
  COMMENT "Generating TXT release notes" VERBATIM)

# Generates HTML documentation
add_custom_target(generate_html
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Documentation ${CMAKE_BINARY_DIR}/Documentation
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/Documentation ${CMAKE_COMMAND}
  -DBUILD_DIR=${CMAKE_BINARY_DIR}
  -DCMAKE_MODULE_PATH=${CMAKE_MODULE_PATH}
  -P generate-html.cmake
  COMMENT "Generating HTML files" VERBATIM)

# NOTE: If you make changes here, ensure they are compatible with the
#       situation in paths.h.in.
if (NOT PACKAGE)
  install(DIRECTORY DESTINATION var/cache/maxscale)
  install(DIRECTORY DESTINATION var/log/maxscale)
  install(DIRECTORY DESTINATION var/run/maxscale)
  install(DIRECTORY DESTINATION var/lib/maxscale)
  install(DIRECTORY DESTINATION ${DEFAULT_MODULE_CONFIG_SUBPATH})
endif()

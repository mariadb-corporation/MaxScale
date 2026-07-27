# libssh is already built by the mariadbmon module (the 'libssh' ExternalProject target is global).
# Re-including cmake/BuildLibSSH.cmake here would redefine that target, so just reuse its outputs.
set(LIBSSH_INSTALL_DIR ${CMAKE_BINARY_DIR}/libssh/install)
set(LIBSSH_LIBRARY ${LIBSSH_INSTALL_DIR}/lib/libssh.a)
set(LIBSSH_INCLUDE_DIR ${LIBSSH_INSTALL_DIR}/include)

add_library(mysqlrepmon SHARED mariadbmon.cc cluster_manipulation.cc cluster_discovery.cc
            monitor_commands.cc mariadbserver.cc server_utils.cc mariadbmon_common.cc ssh_utils.cc)
target_link_libraries(mysqlrepmon maxscale-common ${LIBSSH_LIBRARY})
target_include_directories(mysqlrepmon PRIVATE ${LIBSSH_INCLUDE_DIR})
add_dependencies(mysqlrepmon libssh)

set_target_properties(mysqlrepmon PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(mysqlrepmon core)

add_library(mysqlrepmon SHARED mariadbmon.cc cluster_manipulation.cc cluster_discovery.cc
            monitor_commands.cc mariadbserver.cc server_utils.cc mariadbmon_common.cc ssh_utils.cc)
target_link_libraries(mysqlrepmon maxscale-common ${LIBSSH_LIBRARY})
target_include_directories(mysqlrepmon PRIVATE ${LIBSSH_INCLUDE_DIR})

set_target_properties(mysqlrepmon PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(mysqlrepmon core)

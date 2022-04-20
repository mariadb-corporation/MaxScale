add_library(mariadbmon SHARED mariadbmon.cc cluster_manipulation.cc cluster_discovery.cc
            monitor_commands.cc mariadbserver.cc server_utils.cc mariadbmon_common.cc)
target_link_libraries(mariadbmon maxscale-common)
add_dependencies(mariadbmon pcre2)
set_target_properties(mariadbmon PROPERTIES VERSION "1.4.0" LINK_FLAGS -Wl,-z,defs)
install_module(mariadbmon core)
if(BUILD_TESTS)
    add_subdirectory(test)
endif()

add_library(mariadbmon SHARED mariadbmon.cc mariadbserver.cc cluster_manipulation.cc cluster_discovery.cc
            mariadbmon_common.cc gtid.cc)
target_link_libraries(mariadbmon maxscale-common)
add_dependencies(mariadbmon pcre2)
set_target_properties(mariadbmon PROPERTIES VERSION "1.4.0")
install_module(mariadbmon core)
add_subdirectory(test)

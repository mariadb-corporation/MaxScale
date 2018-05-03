add_library(debugcli SHARED debugcli.cc debugcmd.cc)
target_link_libraries(debugcli maxscale-common)
set_target_properties(debugcli PROPERTIES VERSION "1.1.1")
install_module(debugcli core)


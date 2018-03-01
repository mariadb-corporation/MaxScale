add_library(debugcli SHARED debugcli.c debugcmd.c)
target_link_libraries(debugcli maxscale-common)
set_target_properties(debugcli PROPERTIES VERSION "1.1.1")
install_module(debugcli core)


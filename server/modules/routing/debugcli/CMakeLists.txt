add_library(debugcli SHARED debugcli.cc debugcmd.cc)
target_link_libraries(debugcli maxscale-common)
set_target_properties(debugcli PROPERTIES VERSION "1.1.1")
# Can't add `LINK_FLAGS -Wl,-z,defs` as the module uses maxscale_shutdown() which is defined in gateway.cc
install_module(debugcli core)

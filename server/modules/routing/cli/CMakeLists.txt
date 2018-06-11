add_library(cli SHARED cli.cc ../debugcli/debugcmd.cc)
target_link_libraries(cli maxscale-common)
set_target_properties(cli PROPERTIES VERSION "1.0.0")
# Can't add `LINK_FLAGS -Wl,-z,defs` as the module uses maxscale_shutdown() which is defined in gateway.cc
install_module(cli core)

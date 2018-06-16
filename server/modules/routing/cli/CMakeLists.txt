add_library(cli SHARED cli.cc ../debugcli/debugcmd.cc)
target_link_libraries(cli maxscale-common)
set_target_properties(cli PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(cli core)

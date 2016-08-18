add_library(cli SHARED cli.c ../debugcli/debugcmd.c)
target_link_libraries(cli maxscale-common)
set_target_properties(cli PROPERTIES VERSION "1.0.0")
install_module(cli core)

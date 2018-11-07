add_library(csmon SHARED csmon.cc)
target_link_libraries(csmon maxscale-common)
set_target_properties(csmon PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(csmon core)

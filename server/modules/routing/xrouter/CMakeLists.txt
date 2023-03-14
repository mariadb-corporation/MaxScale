add_library(xrouter SHARED xrouter.cc xroutersession.cc)
target_link_libraries(xrouter maxscale-common)
set_target_properties(xrouter PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
install_module(xrouter core)

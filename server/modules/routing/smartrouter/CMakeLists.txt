add_library(smartrouter SHARED
smartrouter.cc
smartsession.cc
perf_info.cc
perf_updater.cc
)

target_link_libraries(smartrouter maxscale-common)
set_target_properties(smartrouter PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
install_module(smartrouter core)

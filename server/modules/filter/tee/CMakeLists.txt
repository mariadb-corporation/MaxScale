add_library(tee SHARED tee.cc)
target_link_libraries(tee maxscale-common)
set_target_properties(tee PROPERTIES VERSION "1.0.0")
install_module(tee core)

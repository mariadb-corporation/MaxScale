add_library(readwritesplit SHARED readwritesplit.c)
target_link_libraries(readwritesplit maxscale-common)
set_target_properties(readwritesplit PROPERTIES VERSION "1.0.2")
install_module(readwritesplit core)

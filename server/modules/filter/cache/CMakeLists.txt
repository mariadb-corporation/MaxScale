add_library(cache SHARED cache.c storage.c)
target_link_libraries(cache maxscale-common)
set_target_properties(cache PROPERTIES VERSION "1.0.0")
install_module(cache experimental)

add_subdirectory(storage)

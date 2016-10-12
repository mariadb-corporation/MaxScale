add_library(cache SHARED cache.c rules.c storage.c)
target_link_libraries(cache maxscale-common jansson)
set_target_properties(cache PROPERTIES VERSION "1.0.0")
set_target_properties(cache PROPERTIES LINK_FLAGS -Wl,-z,defs)
install_module(cache experimental)

add_subdirectory(storage)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

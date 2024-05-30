add_library(optimistictrx SHARED optimistictrx.cc)
target_link_libraries(optimistictrx maxscale-common)
set_target_properties(optimistictrx PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs CXX_VISIBILITY_PRESET hidden)
install_module(optimistictrx core)

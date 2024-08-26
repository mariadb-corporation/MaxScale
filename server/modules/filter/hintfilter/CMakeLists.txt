add_library(hintfilter SHARED hintfilter.cc hintparser.cc)
set_target_properties(hintfilter PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
target_link_libraries(hintfilter maxscale-common)
install_module(hintfilter core)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
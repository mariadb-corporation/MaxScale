add_library(nullfilter SHARED
  nullfilter.cc
  nullfiltersession.cc
  )
target_link_libraries(nullfilter maxscale-common)
set_target_properties(nullfilter PROPERTIES VERSION "1.0.0")
install_module(nullfilter core)

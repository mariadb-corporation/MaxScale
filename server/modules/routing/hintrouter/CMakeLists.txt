add_library(hintrouter SHARED
  hintrouter.cc
  hintroutersession.cc
  )

target_link_libraries(hintrouter maxscale-common)
set_target_properties(hintrouter PROPERTIES VERSION "1.0.0")
install_module(hintrouter core)

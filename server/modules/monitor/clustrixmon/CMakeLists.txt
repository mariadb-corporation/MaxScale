add_library(clustrixmon SHARED
  clustrix.cc
  clustrixmon.cc
  clustrixmonitor.cc
  )
target_link_libraries(clustrixmon maxscale-common)
set_target_properties(clustrixmon PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(clustrixmon core)

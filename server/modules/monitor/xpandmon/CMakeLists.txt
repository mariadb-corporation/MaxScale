add_library(xpandmon SHARED
  xpand.cc
  xpandmon.cc
  xpandmonitor.cc
  xpandnode.cc
  )
target_link_libraries(xpandmon maxscale-common)
set_target_properties(xpandmon PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(xpandmon core)

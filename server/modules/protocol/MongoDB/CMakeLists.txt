
add_library(mongodbclient SHARED
  clientconnection.cc
  mongodbclient.cc
  protocolmodule.cc
  )
target_link_libraries(mongodbclient maxscale-common)
set_target_properties(mongodbclient PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(mongodbclient core)

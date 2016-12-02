add_library(dbseerroute SHARED dbseerroute.c)
target_link_libraries(dbseerroute maxscale-common)
set_target_properties(dbseerroute PROPERTIES VERSION "1.0.0")
install_module(dbseerroute core)

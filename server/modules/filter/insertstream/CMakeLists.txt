add_library(insertstream SHARED insertstream.c)
target_link_libraries(insertstream maxscale-common MySQLCommon)
set_target_properties(insertstream PROPERTIES VERSION "1.0.0")
install_module(insertstream core)

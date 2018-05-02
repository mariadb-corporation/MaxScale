add_library(insertstream SHARED insertstream.cc)
target_link_libraries(insertstream maxscale-common mysqlcommon)
set_target_properties(insertstream PROPERTIES VERSION "1.0.0")
install_module(insertstream core)

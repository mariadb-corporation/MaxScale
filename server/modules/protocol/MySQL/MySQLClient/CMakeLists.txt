add_library(mysqlclient SHARED mysql_client.cc)
target_link_libraries(mysqlclient maxscale-common mysqlcommon)
set_target_properties(mysqlclient PROPERTIES VERSION "1.0.0")
install_module(mysqlclient core)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

add_library(mariadbclient SHARED mysql_client.cc)
target_link_libraries(mariadbclient maxscale-common mysqlcommon)
set_target_properties(mariadbclient PROPERTIES VERSION "1.0.0")
install_module(mariadbclient core)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

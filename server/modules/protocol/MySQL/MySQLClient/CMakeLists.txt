add_library(MySQLClient SHARED mysql_client.cc)
target_link_libraries(MySQLClient maxscale-common  MySQLCommon)
set_target_properties(MySQLClient PROPERTIES VERSION "1.0.0")
install_module(MySQLClient core)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

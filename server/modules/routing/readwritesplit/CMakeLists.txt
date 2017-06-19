add_library(readwritesplit SHARED
readwritesplit.cc
rwsplit_mysql.cc
rwsplit_route_stmt.cc
rwsplit_select_backends.cc
rwsplit_session_cmd.cc
rwsplit_tmp_table_multi.cc
rwsplit_ps.cc)
target_link_libraries(readwritesplit maxscale-common MySQLCommon)
set_target_properties(readwritesplit PROPERTIES VERSION "1.0.2")
install_module(readwritesplit core)

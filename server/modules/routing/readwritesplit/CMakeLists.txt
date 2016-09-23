add_library(readwritesplit SHARED readwritesplit.c rwsplit_mysql.c rwsplit_route_stmt.c rwsplit_select_backends.c rwsplit_session_cmd.c rwsplit_tmp_table_multi.c)
target_link_libraries(readwritesplit maxscale-common)
set_target_properties(readwritesplit PROPERTIES VERSION "1.0.2")
install_module(readwritesplit core)

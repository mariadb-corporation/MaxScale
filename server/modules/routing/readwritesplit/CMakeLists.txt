add_library(readwritesplit SHARED
readwritesplit.cc
rwsplitsession.cc
rwsplit_route_stmt.cc
rwsplit_select_backends.cc
rwsplit_session_cmd.cc
rwsplit_causal_reads.cc
)
target_link_libraries(readwritesplit maxscale-common mariadbprotocol)
set_target_properties(readwritesplit PROPERTIES VERSION "1.0.2"  LINK_FLAGS -Wl,-z,defs)
install_module(readwritesplit core)

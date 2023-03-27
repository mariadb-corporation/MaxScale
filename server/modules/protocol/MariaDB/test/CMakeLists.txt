# Link to the library sources directly. This way any symbols in it are defined
# once and no unwanted linking happens behind the scenes.
add_executable(test_parse_kill test_parse_kill.cc
  ../local_client.cc
  ../mariadb_backend.cc
  ../mariadb_client.cc
  ../mariadb_common.cc
  ../packet_parser.cc
  ../protocol_module.cc
  ../resultset.cc
  ../rwbackend.cc
  ../sqlite_strlike.cc
  ../user_data.cc
)

add_executable(test_setparser test_setparser.cc)
add_executable(test_setsqlmodeparser test_setsqlmodeparser.cc)
add_executable(test_sqlmodeparser test_sqlmodeparser.cc)

target_link_libraries(test_parse_kill maxscale-common)
target_link_libraries(test_setsqlmodeparser maxscale-common)
target_link_libraries(test_setparser maxscale-common)
target_link_libraries(test_sqlmodeparser maxscale-common)

add_test(test_parse_kill test_parse_kill)
add_test(test_setparser test_setparser)
add_test(test_setsqlmodeparser test_setsqlmodeparser)
add_test(test_sqlmodeparser test_sqlmodeparser)

add_executable(test_sqlite_strlike test_sqlite_strlike.cc ../sqlite_strlike.cc)
target_link_libraries(test_sqlite_strlike maxscale-common)
add_test(test_sqlite_strlike test_sqlite_strlike)

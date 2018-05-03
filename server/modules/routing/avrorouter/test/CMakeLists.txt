add_executable(test_alter_parsing test_alter_parsing.cc)
target_link_libraries(test_alter_parsing maxscale-common ${JANSSON_LIBRARIES} ${AVRO_LIBRARIES} maxavro sqlite3 lzma)
add_test(test_alter_parsing test_alter_parsing)

add_executable(test_alter_parsing test_alter_parsing.cc)
target_link_libraries(test_alter_parsing avro-common maxscale-common ${JANSSON_LIBRARIES} ${AVRO_LIBRARIES} maxavro sqlite3 lzma)
add_test(test_alter_parsing test_alter_parsing)

add_executable(test_tokenizer test_tokenizer.cc)
target_link_libraries(test_tokenizer avro-common maxscale-common ${JANSSON_LIBRARIES} ${AVRO_LIBRARIES} maxavro sqlite3 lzma)
add_test(test_tokenizer test_tokenizer)

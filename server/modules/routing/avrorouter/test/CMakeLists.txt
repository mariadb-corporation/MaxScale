# Don't link to the replicator, it ends up defining some symbols twice.
add_executable(test_tokenizer test_tokenizer.cc ../../replicator/tokenizer.cc)
target_link_libraries(test_tokenizer maxscale-common ${JANSSON_LIBRARIES} ${AVRO_LIBRARIES} maxsql maxavro sqlite3 lzma)
add_test(test_tokenizer test_tokenizer)

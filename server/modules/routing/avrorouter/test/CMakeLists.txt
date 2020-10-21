add_executable(test_tokenizer test_tokenizer.cc)
target_link_libraries(test_tokenizer replicator maxscale-common ${JANSSON_LIBRARIES} ${AVRO_LIBRARIES} maxsql
  maxavro sqlite3 lzma)
add_test(test_tokenizer test_tokenizer)

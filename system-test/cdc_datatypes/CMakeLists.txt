add_test_executable(cdc_datatypes.cc cdc_datatypes avro LABELS avrorouter BREAKS_REPL REPL_BACKEND)
add_library(cdc_result cdc_result.cc)
target_link_libraries(cdc_datatypes cdc_result)

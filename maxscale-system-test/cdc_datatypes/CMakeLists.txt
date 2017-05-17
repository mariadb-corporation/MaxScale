add_test_executable(cdc_datatypes.cpp cdc_datatypes avro LABELS avrorouter binlogrouter BREAKS_REPL)
add_library(cdc_result cdc_result.cpp)
target_link_libraries(cdc_datatypes cdc_result)

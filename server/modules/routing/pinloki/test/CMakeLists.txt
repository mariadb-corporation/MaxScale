add_executable(pinloki_parser parser_test.cc)
target_link_libraries(pinloki_parser pinloki-core)
add_test(test_pinloki_parser pinloki_parser)

add_executable(pinloki_main main.cc)
target_link_libraries(pinloki_main pinloki-core)

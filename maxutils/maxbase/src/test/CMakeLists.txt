add_executable(test_mxb_log test_log.cc)
target_link_libraries(test_mxb_log maxbase)
add_test(test_mxb_log test_mxb_log)


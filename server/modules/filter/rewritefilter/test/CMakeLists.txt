add_executable(test_rewrite test_rewrite.cc)
target_link_libraries(test_rewrite rewritefilter)
add_test(test_rewrite_rf test_rewrite ${CMAKE_CURRENT_SOURCE_DIR}/rewrite.rf)

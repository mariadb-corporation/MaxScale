# Don't link against the filter, that ends up defining some symbols twice.
add_executable(test_rewrite test_rewrite.cc
  ../json_reader.cc
  ../native_replacer.cc
  ../native_rewriter.cc
  ../regex_rewriter.cc
  ../rewritefilter.cc
  ../rewritesession.cc
  ../rf_reader.cc
  ../sql_rewriter.cc
  ../template_reader.cc
)
target_link_libraries(test_rewrite maxscale-common)
add_test(test_rewrite_rf test_rewrite ${CMAKE_CURRENT_SOURCE_DIR}/rewrite.rf)

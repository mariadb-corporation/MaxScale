include_directories(..)

add_executable(test_csxml
  test_csxml.cc
  )

add_executable(cssystemtest
  cssystemtest.cc
  )

target_link_libraries(test_csxml csmon ${LIBXML2_LIBRARIES})

target_link_libraries(cssystemtest maxbase ${JANSSON_LIBRARIES} ${LIBXML2_LIBRARIES})

add_test(test_csxml test_csxml)
add_test(cssystemtest cssystemtest)

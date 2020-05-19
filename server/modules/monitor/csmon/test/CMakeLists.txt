include_directories(..)

add_executable(test_csxml
  test_csxml.cc
  )

target_link_libraries(test_csxml csmon ${LIBXML2_LIBRARIES})

add_test(test_csxml test_csxml)

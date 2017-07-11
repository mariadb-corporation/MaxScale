add_custom_target(test_rest_api
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_rest_api.sh ${CMAKE_SOURCE_DIR}
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

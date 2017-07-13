add_custom_target(test_rest_api
  COMMAND ${CMAKE_SOURCE_DIR}/test/run_npm_test.sh
  ${CMAKE_SOURCE_DIR}           # Path to MaxScale sources
  ${CMAKE_CURRENT_SOURCE_DIR}   # Path to test sources
  ${CMAKE_BINARY_DIR}/rest-api/ # Location where tests are built and run
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

macro(set_maxscale_version)

  set(MAXSCALE_VERSION_MAJOR "1")
  set(MAXSCALE_VERSION_MINOR "0")
  set(MAXSCALE_VERSION_PATCH "0")
  set(MAXSCALE_VERSION "${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}.${MAXSCALE_VERSION_PATCH}")

endmacro()

macro(set_testing_variables)

  if(NOT TEST_LOG)
    set(TEST_LOG "${CMAKE_SOURCE_DIR}/test/test_maxscale.log")
  endif()

  if(NOT TEST_HOST)
    set(TEST_HOST "127.0.0.1")
  endif()

  if(NOT TEST_PORT)
    set(TEST_PORT "4006")
  endif()

  if(NOT TEST_MASTER_ID)
    set(TEST_MASTER_ID "3000")
  endif()

  if(NOT TEST_USER)
    set(TEST_USER "maxuser")
  endif()

  if(NOT TEST_PASSWORD)
    set(TEST_PASSWORD "maxpwd")
  endif()

endmacro()
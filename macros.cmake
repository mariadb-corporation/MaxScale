macro(set_maxscale_version)

  #MaxScale version number
  set(MAXSCALE_VERSION_MAJOR "1")
  set(MAXSCALE_VERSION_MINOR "0")
  set(MAXSCALE_VERSION_PATCH "0")
  set(MAXSCALE_VERSION "${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}.${MAXSCALE_VERSION_PATCH}-beta")

endmacro()

macro(set_testing_variables)

  # hostname or IP address of MaxScale's host
  if(NOT TEST_HOST)
    set(TEST_HOST "127.0.0.1")
  endif()

  # port of read connection router module
  if(NOT TEST_PORT_RW)
    set(TEST_PORT_RW "4008")
  endif()

  # port of read/write split router module
  if(NOT TEST_PORT_RW)
    set(TEST_PORT_RW "4006")
  endif()

  # port of read/write split router module with hints
  if(NOT TEST_PORT_RW_HINT)
    set(TEST_PORT_RW_HINT "4006")
  endif()

  # master's server_id
  if(NOT TEST_MASTER_ID)
    set(TEST_MASTER_ID "3000")
  endif()

  # username of MaxScale user
  if(NOT TEST_USER)
    set(TEST_USER "maxuser")
  endif()

  # password of MaxScale user
  if(NOT TEST_PASSWORD)
    set(TEST_PASSWORD "maxpwd")
  endif()

endmacro()
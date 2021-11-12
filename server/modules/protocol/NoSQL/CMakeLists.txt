include(${CMAKE_SOURCE_DIR}/cmake/BuildMongoCDriver.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/BuildMongoCxxDriver.cmake)

include_directories(${MONGO_C_INCLUDE_DIR})
include_directories(${BSON_C_INCLUDE_DIR})
include_directories(${BSON_CXX_INCLUDE_DIR})

add_library(nosqlprotocol SHARED
  clientconnection.cc
  config.cc
  nosql.cc
  nosqlbase.cc
  nosqlcommand.cc
  nosqlcursor.cc
  nosqldatabase.cc
  nosqlupdateoperator.cc
  nosqlprotocol.cc
  protocolmodule.cc

  checksum.c
  crc32-x86.c
  )
add_dependencies(nosqlprotocol mariadbprotocol mongo-c-driver mongo-cxx-driver)
target_link_libraries(nosqlprotocol
  mariadbprotocol
  maxscale-common
  ${BSON_CXX_LIBRARIES}
  ${BSON_C_LIBRARIES}
  )
set_target_properties(nosqlprotocol PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(nosqlprotocol core)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

include(${CMAKE_SOURCE_DIR}/cmake/BuildMongoCDriver.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/BuildMongoCxxDriver.cmake)

include_directories(${MONGO_C_INCLUDE_DIR})
include_directories(${BSON_C_INCLUDE_DIR})
include_directories(${BSON_CXX_INCLUDE_DIR})

add_library(mongodbprotocol SHARED
  clientconnection.cc
  config.cc
  mxsmongo.cc
  mxsmongocommand.cc
  mxsmongocursor.cc
  mxsmongodatabase.cc
  mongodbclient.cc
  protocolmodule.cc

  checksum.c
  crc32-x86.c
  )
add_dependencies(mongodbprotocol mariadbprotocol mongo-c-driver mongo-cxx-driver)
target_link_libraries(mongodbprotocol
  maxscale-common
  ${BSON_CXX_LIBRARIES}
  ${BSON_C_LIBRARIES}
  )
set_target_properties(mongodbprotocol PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(mongodbprotocol core)

add_subdirectory(test)
add_subdirectory(wire)

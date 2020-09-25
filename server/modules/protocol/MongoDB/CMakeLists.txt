include(${CMAKE_SOURCE_DIR}/cmake/BuildMongoCDriver.cmake)
include_directories(${BSON_C_INCLUDE_DIR})

add_library(mongodbclient SHARED
  clientconnection.cc
  mongodbclient.cc
  protocolmodule.cc
  )
add_dependencies(mongodbclient mongo-c-driver)
target_link_libraries(mongodbclient maxscale-common ${BSON_C_LIBRARIES})
set_target_properties(mongodbclient PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(mongodbclient core)

add_subdirectory(test)

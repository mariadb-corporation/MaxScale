include(${CMAKE_SOURCE_DIR}/cmake/BuildMongoCDriver.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/BuildMongoCxxDriver.cmake)

include_directories(${MONGO_C_INCLUDE_DIR})
# The SRC and BUILD directories need to be added to the include path
# as one internal function is used.
include_directories(${MONGO_C_SRC_DIR})
include_directories(${MONGO_C_BUILD_DIR})
include_directories(${BSON_C_INCLUDE_DIR})
include_directories(${BSON_CXX_INCLUDE_DIR})

add_library(nosqlprotocol SHARED
  clientconnection.cc
  configuration.cc
  nosql.cc
  nosqlbase.cc
  nosqlcommand.cc
  nosqlcommands.cc
  nosqlconfig.cc
  nosqlcontext.cc
  nosqlcrypto.cc
  nosqlcursor.cc
  nosqldatabase.cc
  nosqlprotocol.cc
  nosqlscram.cc
  nosqlupdateoperator.cc
  nosqlusermanager.cc
  protocolmodule.cc

  checksum.c
  crc32-x86.c
  )

# sasl2, resolv and icuuc are needed due to the internal function being used.
add_dependencies(nosqlprotocol mariadbprotocol mongo-c-driver mongo-cxx-driver)
target_link_libraries(nosqlprotocol
  mariadbprotocol
  maxscale-common
  ${MONGO_C_LIBRARIES}
  ${BSON_CXX_LIBRARIES}
  ${BSON_C_LIBRARIES}
  uuid
  sasl2
  resolv
  icuuc
  )
set_target_properties(nosqlprotocol PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(nosqlprotocol core)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

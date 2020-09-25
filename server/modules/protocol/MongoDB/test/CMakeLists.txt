include(${CMAKE_SOURCE_DIR}/cmake/BuildMongoCxxDriver.cmake)
include_directories(${BSON_CXX_INCLUDE_DIR})
include_directories(${MONGO_CXX_INCLUDE_DIR})

if (BUILD_MONGO_CLIENT)
  add_executable(mongoclient
    mongoclient.cc
    )

  add_dependencies(mongoclient mongo-cxx-driver)

  target_link_libraries(mongoclient
    ${MONGO_CXX_LIBRARIES}
    ${BSON_CXX_LIBRARIES}
    ${MONGO_C_LIBRARIES}
    ${BSON_C_LIBRARIES}
    icuuc
    resolv
    sasl2
    ssl
    crypto
    rt
    z
    )

endif()

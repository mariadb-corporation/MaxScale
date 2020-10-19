if (BUILD_MONGO_CLIENT)
  add_executable(mongodump
    mongodump.cc
    ../mxsmongo.cc
    ../mxsmongodatabase.cc
    )

  add_dependencies(mongodump mongo-cxx-driver)

  target_link_libraries(mongodump
    maxscale-common
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


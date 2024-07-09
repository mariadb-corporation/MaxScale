include_directories(..)

# nosqltest_path
add_executable(nosqltest_path nosqltest_path.cc)

target_link_libraries(nosqltest_path
  nosqlprotocol
  maxscale-common
  cache
  ${MONGO_C_LIBRARIES}
  ${BSON_CXX_LIBRARIES}
  ${BSON_C_LIBRARIES}
  uuid
  sasl2
  resolv
  icuuc
)
add_dependencies(nosqltest_path mariadbprotocol mongo-c-driver mongo-cxx-driver)

# nosqltest_roles_from_grants
add_executable(nosqltest_roles_from_grants nosqltest_roles_from_grants.cc)

target_link_libraries(nosqltest_roles_from_grants
  nosqlprotocol
  )
add_dependencies(nosqltest_roles_from_grants mariadbprotocol mongo-c-driver mongo-cxx-driver)

# nosqltest_nobson
add_executable(nosqltest_nobson nosqltest_nobson.cc)

target_link_libraries(nosqltest_nobson
  nosqlprotocol
  maxscale-common
  cache
  ${MONGO_C_LIBRARIES}
  ${BSON_CXX_LIBRARIES}
  ${BSON_C_LIBRARIES}
  uuid
  sasl2
  resolv
  icuuc
)

add_dependencies(nosqltest_nobson mariadbprotocol mongo-c-driver mongo-cxx-driver)

#
# The tests
#
add_test(nosqltest_path nosqltest_path)
add_test(nosqltest_roles_from_grants nosqltest_roles_from_grants)
add_test(nosqltest_nobson nosqltest_nobson)

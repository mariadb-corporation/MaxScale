include_directories(..)

# nosqltest_path
add_executable(nosqltest_path nosqltest_path.cc)

target_link_libraries(nosqltest_path
  nosqlprotocol
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
)

add_dependencies(nosqltest_nobson mariadbprotocol mongo-c-driver mongo-cxx-driver)

#
# The tests
#
add_test(nosqltest_path nosqltest_path)
add_test(nosqltest_roles_from_grants nosqltest_roles_from_grants)
add_test(nosqltest_nobson nosqltest_nobson)

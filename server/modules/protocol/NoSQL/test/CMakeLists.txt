include_directories(..)

add_executable(nosqltest_path nosqltest_path.cc
  ../clientconnection.cc
  ../configuration.cc
  ../nosqlbase.cc
  ../nosql.cc
  ../nosqlcommand.cc
  ../nosqlcommands.cc
  ../nosqlcontext.cc
  ../nosqlcrypto.cc
  ../nosqlcursor.cc
  ../nosqldatabase.cc
  ../nosqlprotocol.cc
  ../nosqlscram.cc
  ../nosqlupdateoperator.cc
  ../nosqlusermanager.cc
  ../protocolmodule.cc
  ../checksum.c
  ../crc32-x86.c
)

target_link_libraries(nosqltest_path maxscale-common
  ${MONGO_C_LIBRARIES}
  ${BSON_CXX_LIBRARIES}
  ${BSON_C_LIBRARIES}
  uuid
  sasl2
  resolv
  icuuc
)
add_dependencies(nosqltest_path mariadbprotocol mongo-c-driver mongo-cxx-driver)

add_executable(nosqltest_roles_from_grants nosqltest_roles_from_grants.cc)

target_link_libraries(nosqltest_roles_from_grants
  nosqlprotocol
  )
add_dependencies(nosqltest_roles_from_grants mariadbprotocol mongo-c-driver mongo-cxx-driver)

add_test(nosqltest_path nosqltest_path)
add_test(nosqltest_roles_from_grants nosqltest_roles_from_grants)

include_directories(..)

add_executable(nosqltest_path nosqltest_path.cc
  ../clientconnection.cc
  ../config.cc
  ../nosql.cc
  ../nosqlbase.cc
  ../nosqlcommand.cc
  ../nosqlcursor.cc
  ../nosqldatabase.cc
  ../nosqlprotocol.cc
  ../protocolmodule.cc

  ../checksum.c
  ../crc32-x86.c

  )
target_link_libraries(nosqltest_path
  maxscale-common
  ${BSON_CXX_LIBRARIES}
  ${BSON_C_LIBRARIES}
  )
add_dependencies(nosqltest_path mariadbprotocol mongo-c-driver mongo-cxx-driver)

add_test(nosqltest_path nosqltest_path)

include_directories(..)

add_executable(nosqltest_path nosqltest_path.cc)

# All of these are most likely not needed, but maxscale-common alone
# is not sufficient and I got tired of trying to figure out exactly
# which ones are essential.
target_link_libraries(nosqltest_path
  nosqlprotocol
  ${BSON_CXX_LIBRARIES}
  ${BSON_C_LIBRARIES}
  maxscale-common
  maxbase
  maxsql
  maxsimd
  ${MARIADB_CONNECTOR_LIBRARIES}
  ${LZMA_LINK_FLAGS}
  ${PCRE2_LIBRARIES}
  ${JANSSON_LIBRARIES}
  ssl
  pthread
  crypt
  crypto
  inih
  z
  rt
  m
  sqlite3
  stdc++
  ${MICROHTTPD_LIBRARIES}
  ${GNUTLS_LIBRARIES}
  ${LIBATOMIC_LIBRARIES}
  mariadbprotocol
  mariadbauth
  )
add_dependencies(nosqltest_path mariadbprotocol mongo-c-driver mongo-cxx-driver)

add_test(nosqltest_path nosqltest_path)

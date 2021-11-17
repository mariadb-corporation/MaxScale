include_directories(..)

add_executable(nosqltest_path nosqltest_path.cc)

# All of these are most likely not needed, but maxscale-common alone
# is not sufficient and I got tired of trying to figure out exactly
# which ones are essential.
target_link_libraries(nosqltest_path
  nosqlprotocol
  )
add_dependencies(nosqltest_path mariadbprotocol mongo-c-driver mongo-cxx-driver)

add_test(nosqltest_path nosqltest_path)

include_directories(..)

add_executable(test_threadpool
  test_threadpool.cc
  ../threadpool.cc
  )

target_link_libraries(test_threadpool maxscale-common)

add_test(test_threadpool test_threadpool)

include_directories(..)

add_executable(testrules testrules.cc ../rules.cc)
target_link_libraries(testrules maxscale-common jansson)

add_executable(testkeygeneration
  testkeygeneration.cc
  ../../../../../query_classifier/test/testreader.cc
  )
target_link_libraries(testkeygeneration maxscale-common cache)

add_executable(testrawstorage
  testrawstorage.cc
  ../../../../../query_classifier/test/testreader.cc
  )
target_link_libraries(testrawstorage maxscale-common cache)

add_test(TestCache_rules testrules)

add_test(TestCache_keygeneration testkeygeneration storage_inmemory ${CMAKE_CURRENT_SOURCE_DIR}/input.test)
add_test(TestCache_keygeneration testkeygeneration storage_rocksdb ${CMAKE_CURRENT_SOURCE_DIR}/input.test)

add_test(TestCache_storage_inmemory testrawstorage 10 storage_inmemory ${CMAKE_CURRENT_SOURCE_DIR}/input.test)
add_test(TestCache_storage_rocksdb  testrawstorage 10 storage_rocksdb ${CMAKE_CURRENT_SOURCE_DIR}/input.test)

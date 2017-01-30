include(BuildRocksDB.cmake)

if (ROCKSDB_BUILT)
  message(STATUS "RocksDB is built, storage_rocksdb will be built.")

  set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS} -DROCKSDB_PLATFORM_POSIX")
  set(CMAKE_CXX_FLAGS_DEBUG "-std=c++11 ${CMAKE_CXX_FLAGS_DEBUG} -DROCKSDB_PLATFORM_POSIX")
  set(CMAKE_CXX_FLAGS_RELEASE "-std=c++11 ${CMAKE_CXX_FLAGS_RELEASE} -DROCKSDB_PLATFORM_POSIX")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-std=c++11 ${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DROCKSDB_PLATFORM_POSIX")

  include_directories(${ROCKSDB_INCLUDE_DIRS})
  link_directories(${ROCKSDB_LIB_DIR})

  add_library(storage_rocksdb SHARED
    rocksdbinternals.cc
    rocksdbstorage.cc
    storage_rocksdb.cc
    )
  add_dependencies(storage_rocksdb RocksDB)
  target_link_libraries(storage_rocksdb maxscale-common ${JANSSON_LIBRARIES} ${ROCKSDB_LIB} ${ROCKSDB_LINK_LIBS})
  set_target_properties(storage_rocksdb PROPERTIES VERSION "1.0.0")
  set_target_properties(storage_rocksdb PROPERTIES LINK_FLAGS -Wl,-z,defs)
  install_module(storage_rocksdb core)
else()
  message("RocksDB not built, storage_rocksdb cannot be built.")
endif()

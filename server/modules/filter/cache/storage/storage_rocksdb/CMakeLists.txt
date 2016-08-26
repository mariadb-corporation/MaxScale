include(BuildRocksDB.cmake)

if (ROCKSDB_BUILT)
  message(STATUS "RocksDB is built, storage_rocksdb will be built.")

  set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS_DEBUG "-std=c++11 ${CMAKE_CXX_FLAGS_DEBUG}")
  set(CMAKE_CXX_FLAGS_RELEASE "-std=c++11 ${CMAKE_CXX_FLAGS_RELEASE}")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-std=c++11 ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

  include_directories(${ROCKSDB_INCLUDE_DIR})
  link_directories(${ROCKSDB_LIB_DIR})

  add_library(storage_rocksdb SHARED
    rocksdbstorage.cc
    storage_rocksdb.cc
    )
  target_link_libraries(storage_rocksdb maxscale-common ${ROCKSDB_LIB})
  set_target_properties(storage_rocksdb PROPERTIES VERSION "1.0.0")
  set_target_properties(storage_rocksdb PROPERTIES LINK_FLAGS -Wl,-z,defs)
  install_module(storage_rocksdb experimental)
else()
  message("RocksDB not built, storage_rocksdb cannot be built.")
endif()

# Build RocksDB

if ((CMAKE_CXX_COMPILER_ID STREQUAL "GNU") AND (NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)))
  message(STATUS "GCC >= 4.8, RocksDB is built.")

  set(ROCKSDB_REPO "https://github.com/facebook/rocksdb.git"
    CACHE STRING "RocksDB Git repository")

  # Release 4.9 of RocksDB
  set(ROCKSDB_TAG "v4.9"
    CACHE STRING "RocksDB Git tag")

  set(ROCKSDB_SUBPATH "/server/modules/filter/cache/storage/storage_rocksdb/RocksDB-prefix/src/RocksDB")
  set(ROCKSDB_ROOT    ${CMAKE_BINARY_DIR}${ROCKSDB_SUBPATH})

  ExternalProject_Add(RocksDB
    GIT_REPOSITORY ${ROCKSDB_REPO}
    GIT_TAG ${ROCKSDB_TAG}
    CONFIGURE_COMMAND ""
    BINARY_DIR ${ROCKSDB_ROOT}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make DISABLE_JEMALLOC=1 EXTRA_CXXFLAGS=-fPIC static_lib
    INSTALL_COMMAND "")

  set(ROCKSDB_BUILT TRUE CACHE INTERNAL "")
  set(ROCKSDB_INCLUDE_DIRS ${ROCKSDB_ROOT}/include ${ROCKSDB_ROOT})
  set(ROCKSDB_LIB_DIR ${ROCKSDB_ROOT})
  set(ROCKSDB_LIB librocksdb.a)

  # RocksDB supports several compression libraries and automatically
  # uses them if it finds the headers in the environment. Consequently,
  # we must ensure that a user of RocksDB can link to the needed
  # libraries.
  #
  # ROCKSDB_LINK_LIBS specifies that libraries a module using ROCKSDB_LIB
  # must link with.

  find_package(BZip2)
  if (BZIP2_FOUND)
    set(ROCKSDB_LINK_LIBS ${ROCKSDB_LINK_LIBS} ${BZIP2_LIBRARIES})
  endif()

  find_package(ZLIB)
  if (ZLIB_FOUND)
    set(ROCKSDB_LINK_LIBS ${ROCKSDB_LINK_LIBS} ${ZLIB_LIBRARIES})
  endif()
else()
  message(STATUS "RocksDB requires GCC >= 4.7, only ${CMAKE_CXX_COMPILER_VERSION} available.")
endif()

add_subdirectory(storage_inmemory)

if (BUILD_STORAGE_MEMCACHED)
  add_subdirectory(storage_memcached)
endif()

if (BUILD_STORAGE_REDIS)
  add_subdirectory(storage_redis)
endif()

add_subdirectory(storage_inmemory)

if (BUILD_STORAGE_MEMCACHED)
  add_subdirectory(storage_memcached)
endif()

if (BUILD_STORAGE_REDIS)
  if (HIREDIS_FOUND)
    add_subdirectory(storage_redis)
  else()
    message(ERROR "Although requested, storage_redis cannot be built as hiredis not available.")
  endif()
endif()

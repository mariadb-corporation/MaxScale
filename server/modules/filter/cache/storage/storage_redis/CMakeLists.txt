find_package(Hiredis)

if (NOT HIREDIS_FOUND AND BUNDLE)
  include(${CMAKE_SOURCE_DIR}/cmake/BuildHiredis.cmake)
endif()

if (HIREDIS_FOUND)
  include_directories(${HIREDIS_INCLUDE_DIR})
  add_library(storage_redis SHARED
    redisconfig.cc
    redisstorage.cc
    storage_redis.cc
  )

  target_link_libraries(storage_redis cache maxscale-common ${HIREDIS_LIBRARIES})
  set_target_properties(storage_redis PROPERTIES VERSION "1.0.0")
  set_target_properties(storage_redis PROPERTIES LINK_FLAGS -Wl,-z,defs)
  install_module(storage_redis core)
else()
  message(STATUS "Could not find hiredis, cannot build storage_redis")
endif()

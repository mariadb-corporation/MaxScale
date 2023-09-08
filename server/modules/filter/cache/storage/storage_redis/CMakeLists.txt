include(${CMAKE_SOURCE_DIR}/cmake/BuildHiredis.cmake)

include_directories(${HIREDIS_INCLUDE_DIR})

add_library(storage_redis SHARED
  redisconfig.cc
  redisstorage.cc
  storage_redis.cc
  )

add_dependencies(storage_redis hiredis)
target_link_libraries(storage_redis cache maxscale-common ${HIREDIS_STATIC_LIBRARIES})
set_target_properties(storage_redis PROPERTIES VERSION "1.0.0")
set_target_properties(storage_redis PROPERTIES LINK_FLAGS -Wl,-z,defs)
install_module(storage_redis core)

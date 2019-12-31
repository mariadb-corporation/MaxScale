include_directories(${HIREDIS_INCLUDE_DIR})

add_library(storage_redis SHARED
  redisstorage.cc
  storage_redis.cc
  )

target_link_libraries(storage_redis cache maxscale-common ${HIREDIS_LIBRARIES})
set_target_properties(storage_redis PROPERTIES VERSION "1.0.0")
set_target_properties(storage_redis PROPERTIES LINK_FLAGS -Wl,-z,defs)
install_module(storage_redis core)

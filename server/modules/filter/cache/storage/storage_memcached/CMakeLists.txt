include(${CMAKE_SOURCE_DIR}/cmake/BuildLibMemcached.cmake)
include_directories(${LIBMEMCACHED_INCLUDE_DIR})

add_library(storage_memcached SHARED
  memcachedstorage.cc
  storage_memcached.cc
  )

target_link_libraries(storage_memcached cache maxscale-common ${LIBMEMCACHED_LIBRARIES} sasl2)
set_target_properties(storage_memcached PROPERTIES VERSION "1.0.0")
set_target_properties(storage_memcached PROPERTIES LINK_FLAGS -Wl,-z,defs)
install_module(storage_memcached core)

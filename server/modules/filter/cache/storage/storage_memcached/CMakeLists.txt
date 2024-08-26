find_library(HAVE_SASL NAMES sasl2)
find_package(LibMemcached)

if (NOT LIBMEMCACHED_FOUND AND BUNDLE)
  include(${CMAKE_SOURCE_DIR}/cmake/BuildLibMemcached.cmake)
endif()

if(HAVE_SASL AND LIBMEMCACHED_FOUND)
  include_directories(${LIBMEMCACHED_INCLUDE_DIR})

  add_library(storage_memcached SHARED
    memcachedstorage.cc
    storage_memcached.cc
    )

  target_link_libraries(storage_memcached cache maxscale-common ${LIBMEMCACHED_LIBRARIES} sasl2)
  set_target_properties(storage_memcached PROPERTIES VERSION "1.0.0")
  set_target_properties(storage_memcached PROPERTIES LINK_FLAGS -Wl,-z,defs)
  install_module(storage_memcached core)
elseif(BUNDLE)
  # This is a fatal error as we didn't find the libraries in the system and we
  # weren't able to build them.
  message(FATAL_ERROR "Could not find sasl2 and libmemcached")
else()
  # Packages weren't found but we're not bundling them, don't build
  # storage_memcached.
  message(STATUS "Not building storage_memcached, sasl2 and libmemcached weren't found")
endif()

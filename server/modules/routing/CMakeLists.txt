# These modules require librdkafka
if (BUILD_MIRROR)
  include(${CMAKE_SOURCE_DIR}/cmake/BuildRdkafka.cmake)
  include_directories(${RDKAFKA_INCLUDE_DIR})
endif()

# Avrorouter uses the replicator library
if(BUILD_CDC)
  add_subdirectory(replicator)
endif()

if(BUILD_CDC)
  add_subdirectory(avrorouter)
endif()

if(BUILD_BINLOG)
  add_subdirectory(binlogrouter)
endif()

add_subdirectory(cat)
add_subdirectory(hintrouter)
add_subdirectory(maxinfo)
add_subdirectory(readconnroute)
add_subdirectory(readwritesplit)
add_subdirectory(schemarouter)
add_subdirectory(smartrouter)

if (BUILD_MIRROR)
  add_subdirectory(mirror)
endif()

# Avrorouter and kafkacdc use the replicator library
if(BUILD_CDC OR BUILD_KAFKACDC)
  add_subdirectory(replicator)
endif()

if(BUILD_CDC)
  add_subdirectory(avrorouter)
endif()

if(BUILD_KAFKACDC)
  add_subdirectory(kafkacdc)
endif()

if(BUILD_BINLOG)
  add_subdirectory(binlogrouter)
endif()

add_subdirectory(cat)
add_subdirectory(hintrouter)
add_subdirectory(readconnroute)
add_subdirectory(readwritesplit)
add_subdirectory(schemarouter)
add_subdirectory(smartrouter)
add_subdirectory(pinloki)

if (BUILD_MIRROR)
  add_subdirectory(mirror)
endif()

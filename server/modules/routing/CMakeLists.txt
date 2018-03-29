if(BUILD_CDC)
  add_subdirectory(avrorouter)
endif()
if(BUILD_BINLOG)
  add_subdirectory(binlogrouter)
endif()

add_subdirectory(cli)
add_subdirectory(debugcli)
add_subdirectory(hintrouter)
add_subdirectory(maxinfo)
add_subdirectory(readconnroute)
add_subdirectory(readwritesplit)
add_subdirectory(schemarouter)
add_subdirectory(cat)



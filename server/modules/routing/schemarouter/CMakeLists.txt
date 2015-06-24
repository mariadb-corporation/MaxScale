add_library(schemarouter SHARED schemarouter.c sharding_common.c)
target_link_libraries(schemarouter log_manager utils query_classifier)
install(TARGETS schemarouter DESTINATION modules)

add_library(shardrouter SHARED shardrouter.c svcconn.c sharding_common.c)
target_link_libraries(shardrouter log_manager utils query_classifier)
install(TARGETS shardrouter DESTINATION modules)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

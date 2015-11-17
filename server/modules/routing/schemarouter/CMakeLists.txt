add_library(schemarouter SHARED schemarouter.c sharding_common.c)
target_link_libraries(schemarouter log_manager query_classifier)
add_dependencies(schemarouter pcre2)
install(TARGETS schemarouter DESTINATION ${MAXSCALE_LIBDIR})

add_library(shardrouter SHARED shardrouter.c svcconn.c sharding_common.c)
target_link_libraries(shardrouter log_manager query_classifier)
add_dependencies(shardrouter pcre2)
install(TARGETS shardrouter DESTINATION ${MAXSCALE_LIBDIR})

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

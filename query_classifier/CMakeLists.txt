add_library(query_classifier SHARED query_classifier.cc)

target_link_libraries(query_classifier ${MYSQL_EMBEDDED_LIB} aio crypt crypto dl m ${PCRE_LINK_FLAGS} ssl stdc++ z)
set_target_properties(query_classifier PROPERTIES VERSION "1.0.0")
set_target_properties(query_classifier PROPERTIES LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/query_classifier.map)
#set_target_properties(query_classifier PROPERTIES LINK_FLAGS -Wl,-z,defs)
install(TARGETS query_classifier COMPONENT lib DESTINATION ${MAXSCALE_LIBDIR})

add_subdirectory(qc_mysqlembedded)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

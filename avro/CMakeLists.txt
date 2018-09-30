if (AVRO_FOUND AND JANSSON_FOUND)
  include_directories(${CMAKE_CURRENT_SOURCE_DIR})
  add_library(maxavro maxavro.c maxavro_schema.c maxavro_record.c maxavro_file.c)
  target_link_libraries(maxavro maxscale-common ${JANSSON_LIBRARIES})

  if(WITH_ASAN AND ASAN_FOUND)
    target_link_libraries(maxavro ${ASAN_LIBRARIES})
  endif()

  add_executable(maxavrocheck maxavrocheck.c)
  target_link_libraries(maxavrocheck maxavro maxscale-common)
  install_executable(maxavrocheck core)
  add_subdirectory(test)
endif()

if(AVRO_FOUND AND JANSSON_FOUND)
  include_directories(${AVRO_INCLUDE_DIR})
  include_directories(${JANSSON_INCLUDE_DIR})

  # The common avrorouter functionality
  add_library(avro-common SHARED avro.cc ../binlogrouter/binlog_common.cc avro_client.cc
              avro_schema.cc avro_rbr.cc avro_file.cc avro_index.cc avro_converter.cc)
  set_target_properties(avro-common PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
  target_link_libraries(avro-common maxscale-common ${JANSSON_LIBRARIES} ${AVRO_LIBRARIES} maxavro lzma)
  install_module(avro-common core)

  # The actual avrorouter implementation
  add_library(avrorouter SHARED avro_main.cc)
  set_target_properties(avrorouter PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
  target_link_libraries(avrorouter avro-common)
  install_module(avrorouter core)

  if (BUILD_TESTS)
    add_subdirectory(test)
  endif()
else()
  message(FATAL_ERROR "No Avro C or Jansson libraries found, cannot build avrorouter. "
    "Use the -DBUILD_CDC=N  option to `cmake` to disable building of the avrorouter.")
endif()

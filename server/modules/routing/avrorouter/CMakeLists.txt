include(${CMAKE_SOURCE_DIR}/cmake/BuildAvro.cmake)

include_directories(${AVRO_INCLUDE_DIR})
include_directories(${JANSSON_INCLUDE_DIR})
include_directories(avro)

add_subdirectory(avro)

add_library(avrorouter SHARED avro_main.cc avro.cc avro_client.cc avro_file.cc avro_converter.cc)
add_dependencies(avrorouter replicator avro-c)
set_target_properties(avrorouter PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
target_link_libraries(avrorouter replicator maxscale-common ${JANSSON_LIBRARIES} ${AVRO_LIBRARIES} maxavro lzma cdc)
install_module(avrorouter core)

if (BUILD_TESTS)
  add_subdirectory(test)
endif()

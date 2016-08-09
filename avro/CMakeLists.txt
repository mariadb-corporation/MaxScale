include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(maxavro maxavro.c maxavro_schema.c maxavro_record.c maxavro_file.c)
target_link_libraries(maxavro maxscale-common jansson)

add_executable(maxavrocheck maxavrocheck.c)
target_link_libraries(maxavrocheck maxavro)
install(TARGETS maxavrocheck DESTINATION ${MAXSCALE_BINDIR})
add_subdirectory(test)

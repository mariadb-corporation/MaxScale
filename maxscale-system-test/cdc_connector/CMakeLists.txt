include(cmake/BuildJansson.cmake)

include_directories(${JANSSON_INCLUDE_DIR})
add_library(cdc_connector STATIC cdc_connector.cpp)
target_link_libraries(cdc_connector ${JANSSON_LIBRARIES})
add_dependencies(cdc_connector jansson)
install(TARGETS cdc_connector DESTINATION lib)
install(FILES cdc_connector.h DESTINATION include)

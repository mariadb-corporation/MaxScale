include(cmake/BuildJansson.cmake)

set(CMAKE_CXX_FLAGS "-std=c++0x")
set(CMAKE_CXX_FLAGS_DEBUG "-std=c++0x -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-std=c++0x -O2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-std=c++0x -O2")

include_directories(${JANSSON_INCLUDE_DIR})
add_library(cdc_connector STATIC cdc_connector.cpp)
target_link_libraries(cdc_connector ${JANSSON_LIBRARIES})
add_dependencies(cdc_connector jansson)
install(TARGETS cdc_connector DESTINATION lib)
install(FILES cdc_connector.h DESTINATION include)

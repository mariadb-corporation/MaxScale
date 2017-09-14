project(cdc_connector)
cmake_minimum_required(VERSION 2.8)

enable_testing()

include(ExternalProject)
include(cmake/BuildJansson.cmake)

set(CMAKE_CXX_FLAGS "-fPIC -std=c++0x")
set(CMAKE_CXX_FLAGS_DEBUG "-fPIC -std=c++0x -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-fPIC -std=c++0x -O2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-fPIC -std=c++0x -O2")

include_directories(${JANSSON_INCLUDE_DIR})
add_library(cdc_connector SHARED cdc_connector.cpp)
target_link_libraries(cdc_connector ${JANSSON_LIBRARIES} crypto)
add_dependencies(cdc_connector jansson)
install(TARGETS cdc_connector DESTINATION lib)
install(FILES cdc_connector.h DESTINATION include)

include(CTest)

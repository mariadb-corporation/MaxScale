project(cdc_connector)
cmake_minimum_required(VERSION 2.8)

include(GNUInstallDirs)

set(CMAKE_CXX_FLAGS "-fPIC -std=c++0x")
set(CMAKE_CXX_FLAGS_DEBUG "-fPIC -std=c++0x -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-fPIC -std=c++0x -O2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-fPIC -std=c++0x -O2")

add_library(cdc_connector STATIC cdc_connector.cpp)

install(TARGETS cdc_connector DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES cdc_connector.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

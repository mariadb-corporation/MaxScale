include_directories(${MARIADB_CONNECTOR_INCLUDE_DIR})

add_library(roundrobinrouter SHARED roundrobinrouter.cpp)
target_link_libraries(roundrobinrouter maxscale-common)
set_target_properties(roundrobinrouter PROPERTIES VERSION "1.0.0")
install_module(roundrobinrouter core)

add_library(examplecppfilter SHARED examplefilter.cc examplefiltersession.cc)
set_target_properties(examplecppfilter PROPERTIES VERSION "1.0.0")
install_module(examplecppfilter core)

include_directories(${MARIADB_CONNECTOR_INCLUDE_DIR})

add_library(roundrobinrouter SHARED roundrobinrouter.cpp)
target_link_libraries(roundrobinrouter maxscale-common)
set_target_properties(roundrobinrouter PROPERTIES VERSION "1.0.0")
install_module(roundrobinrouter core)

add_library(testroute SHARED testroute.c)
target_link_libraries(testroute maxscale-common)
set_target_properties(testroute PROPERTIES VERSION "1.0.0")
install_module(testroute core)

add_library(testfilter SHARED testfilter.c)
target_link_libraries(testfilter maxscale-common)
set_target_properties(testfilter PROPERTIES VERSION "1.0.0")
install_module(testfilter core)

add_library(examplecppfilter SHARED examplefilter.cc examplefiltersession.cc)
set_target_properties(examplecppfilter PROPERTIES VERSION "1.0.0")
install_module(examplecppfilter core)

add_library(testprotocol SHARED testprotocol.c)
set_target_properties(testprotocol PROPERTIES VERSION "1.0.0")
install_module(testprotocol core)

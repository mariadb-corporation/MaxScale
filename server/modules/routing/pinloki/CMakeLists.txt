# Boost 1.73
ExternalProject_Add(boost
  URL "${CMAKE_CURRENT_SOURCE_DIR}/boost-1.73.0.tar.gz"
  SOURCE_DIR ${CMAKE_BINARY_DIR}/boost/
  BINARY_DIR ${CMAKE_BINARY_DIR}/boost/
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

# Make sure we include the custom headers before the system boost headers
include_directories(BEFORE ${CMAKE_BINARY_DIR}/boost/)

add_library(pinloki-core
  config.cc
  dbconnection.cc
  file_reader.cc
  file_writer.cc
  find_gtid.cc
  gtid.cc
  inventory.cc
  maria_rpl_event.cc
  reader.cc
  resultset.cc
  rpl_event.cc
  writer.cc
  parser.cc
  )

add_dependencies(pinloki-core boost)
target_link_libraries(pinloki-core maxbase mariadbprotocol uuid)

add_library(binlogrouter SHARED pinloki.cc pinlokisession.cc)
target_link_libraries(binlogrouter pinloki-core maxscale-common)
set_target_properties(binlogrouter PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
install_module(binlogrouter core)

if (BUILD_TESTS)
  add_subdirectory(test)
endif()

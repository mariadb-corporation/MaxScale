find_package(Boost REQUIRED)

add_library(pinloki-core OBJECT
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
  transaction.cc
  )

target_link_libraries(pinloki-core maxscale-common uuid)

add_library(binlogrouter SHARED pinloki.cc pinlokisession.cc)
target_link_libraries(binlogrouter pinloki-core maxscale-common)
set_target_properties(binlogrouter PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
install_module(binlogrouter core)

if (BUILD_TESTS)
  add_subdirectory(test)
endif()

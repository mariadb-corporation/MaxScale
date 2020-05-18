# This hint points the FindBoost.cmake to the location where the newer Boost
# version is installed on CentOS 7 by the boost169-devel package.
set(BOOST_INCLUDEDIR "/usr/include/boost169")

find_package(Boost 1.60)

if (Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})

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

  target_link_libraries(pinloki-core maxbase mariadbclient uuid)

  add_library(pinloki SHARED pinloki.cc pinlokisession.cc)
  target_link_libraries(pinloki pinloki-core maxscale-common)
  set_target_properties(pinloki PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
  install_module(pinloki core)

  if (BUILD_TESTS)
    add_subdirectory(test)
  endif()
else()
  message(FATAL_ERROR "Boost 1.60 or newer is not installed, cannot build Pinloki")
endif()

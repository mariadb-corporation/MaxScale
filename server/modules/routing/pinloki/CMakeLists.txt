# Pinloki is currently a test executable

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

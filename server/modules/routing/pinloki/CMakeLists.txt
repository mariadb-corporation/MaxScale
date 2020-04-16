# Pinloki is currently a test executable

add_executable(pinloki
    config.cc
    dbconnection.cc
    file_reader.cc
    file_writer.cc
    find_gtid.cc
    gtid.cc
    inventory.cc
    main.cc
    maria_rpl_event.cc
    reader.cc
    resultset.cc
    rpl_event.cc
    worker.cc
    writer.cc
)

target_link_libraries(pinloki maxbase mariadbclient)

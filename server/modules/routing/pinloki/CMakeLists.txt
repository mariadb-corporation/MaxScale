# Pinloki is currently a test executable

add_executable(pinloki
config.cc        file_writer.cc  inventory.cc        reader.cc     worker.cc
dbconnection.cc  find_gtid.cc    main.cc             resultset.cc  writer.cc
file_reader.cc   gtid.cc         maria_rpl_event.cc  rpl_event.cc
)

target_link_libraries(pinloki maxbase mariadbclient)

add_library(maxtest SHARED
        big_load.cc
        blob_test.cc
        xpand_nodes.cpp
        config_operations.cc
        different_size.cc
        envv.cc
        execute_cmd.cc
        fw_copy_rules.cc
        get_com_select_insert.cc
        get_my_ip.cc
        json.cc
        log.cc
        mariadb_func.cc
        mariadb_connector.cc
        mariadb_nodes.cc
        maxinfo_func.cc
        maxrest.cc
        maxscales.cc
        nodes.cc
        rds_vpc.cc
        sql_t1.cc
        tcp_connection.cc
        testconnections.cc
        ${CMAKE_CURRENT_BINARY_DIR}/test_info.cc)

target_link_libraries(maxtest
  ${RDKAFKA_LIBRARIES}
  ${JANSSON_LIBRARIES}
  cdc_connector
  maxsql
  maxbase
  pthread
  crypt
  crypto
  z
  rt
  m)
set_target_properties(maxtest PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install(TARGETS maxtest DESTINATION system-test)
add_dependencies(maxtest jansson librdkafka)

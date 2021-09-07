add_library(maxtest SHARED
        big_load.cc
        blob_test.cc
        cdc_tools.cc
        config_operations.cc
        different_size.cc
        envv.cc
        execute_cmd.cc
        galera_cluster.cc
        get_com_select_insert.cc
        get_my_ip.cc
        json.cc
        log.cc
        mariadb_func.cc
        mariadb_connector.cc
        mariadb_nodes.cc
        maxrest.cc
        maxscales.cc
        nodes.cc
        replication_cluster.cc
        sql_t1.cc
        tcp_connection.cc
        testconnections.cc
        xpand_nodes.cpp
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

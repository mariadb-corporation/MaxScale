add_library(maxtest SHARED
        big_load.cc
        blob_test.cc
        clustrix_nodes.cpp
        config_operations.cc
        different_size.cc
        envv.cc
        execute_cmd.cc
        fw_copy_rules.cc
        get_com_select_insert.cc
        get_my_ip.cc
        mariadb_func.cc
        mariadb_nodes.cc
        maxinfo_func.cc
        maxrest.cc
        maxscales.cc
        nodes.cc
        rds_vpc.cc
        sql_t1.cc
        tcp_connection.cc
        test_binlog_fnc.cc
        testconnections.cc
        ${CMAKE_CURRENT_BINARY_DIR}/test_info.cc
        # Include the CDC connector in the core library
        ${CMAKE_SOURCE_DIR}/connectors/cdc-connector/cdc_connector.cpp)

target_link_libraries(maxtest ${MARIADB_CONNECTOR_LIBRARIES} ${JANSSON_LIBRARIES} maxbase maxsql z m pthread
        ssl dl rt crypto crypt ${RDKAFKA_LIBRARIES})
set_target_properties(maxtest PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install(TARGETS maxtest DESTINATION system-test)
add_dependencies(maxtest connector-c jansson maxbase librdkafka)

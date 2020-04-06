add_library(maxtest SHARED
        big_load.cc
        big_transaction.cc
        blob_test.cc
        config_operations.cc
        different_size.cc
        envv.cc
        execute_cmd.cc
        fw_copy_rules.cc
        get_com_select_insert.cc
        get_my_ip.cc
        keepalived_func.cc
        mariadb_func.cc
        mariadb_nodes.cc
        maxadmin_operations.cc
        maxinfo_func.cc
        maxscales.cc
        nodes.cc
        rds_vpc.cc
        sql_t1.cc
        stopwatch.cc
        tcp_connection.cc
        test_binlog_fnc.cc
        testconnections.cc
        ${CMAKE_CURRENT_BINARY_DIR}/test_info.cc
        # Include the CDC connector in the core library
        ${CMAKE_SOURCE_DIR}/connectors/cdc-connector/cdc_connector.cpp)
target_link_libraries(maxtest ${MARIADB_CONNECTOR_LIBRARIES} ${JANSSON_LIBRARIES} z m pthread ssl dl rt crypto crypt maxbase)
set_target_properties(maxtest PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install(TARGETS maxtest DESTINATION system-test)
add_dependencies(maxtest connector-c jansson maxbase)

add_library(maxtest SHARED
        stopwatch.cpp
        big_load.cpp
        big_transaction.cpp
        blob_test.cpp
        config_operations.cpp
        different_size.cpp
        envv.cpp
        execute_cmd.cpp
        fw_copy_rules.cpp
        get_com_select_insert.cpp
        get_my_ip.cpp
        keepalived_func.cpp
        labels_table.cpp
        mariadb_func.cpp
        mariadb_nodes.cpp
        maxadmin_operations.cpp
        maxinfo_func.cpp
        maxscales.cpp
        nodes.cpp
        rds_vpc.cpp
        sql_t1.cpp
        tcp_connection.cpp
        test_binlog_fnc.cpp
        testconnections.cpp
        # Include the CDC connector in the core library
        ${CMAKE_SOURCE_DIR}/connectors/cdc-connector/cdc_connector.cpp)
target_link_libraries(maxtest ${MARIADB_CONNECTOR_LIBRARIES} ${JANSSON_LIBRARIES} z m pthread ssl dl rt crypto crypt maxbase)
set_target_properties(maxtest PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install(TARGETS maxtest DESTINATION system-test)
add_dependencies(maxtest connector-c jansson maxbase)

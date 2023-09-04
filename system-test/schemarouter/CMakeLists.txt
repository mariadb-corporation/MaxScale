# TODO: Check these all for replication breaking. Fix at the end of test so next test starts faster.

# MXS-1310 Schemarouter implicit database detection
add_test_executable_ex(NAME schemar_implicit_db SOURCE schemar_implicit_db.cc
        CONFIG schemar_implicit_db.cnf VMS repl_backend LABELS schemarouter)

add_test_executable_ex(NAME schemar_many_connections SOURCE schemar_many_connections.cc
        CONFIG sharding.cnf VMS repl_backend LABELS schemarouter)

# MXS-1713: SchemaRouter unable to process SHOW DATABASES for a lot of schemas
# https://jira.mariadb.org/browse/MXS-1713
add_test_executable_ex(NAME schemar_lots_of_databases SOURCE mxs1713_lots_of_databases.cc
        CONFIG mxs1713_lots_of_databases.cnf VMS repl_backend LABELS schemarouter)

# Schemarouter duplicate database detection test: create DB on all nodes and then try query againt schema router
add_test_executable_ex(NAME schemarouter_duplicate SOURCE schemarouter_duplicate.cc
        CONFIG schemarouter_duplicate.cnf VMS repl_backend LABELS schemarouter)

# Test of schema router
add_test_executable_ex(NAME sharding SOURCE sharding.cc CONFIG sharding.cnf
        VMS repl_backend LABELS schemarouter)

# MXS-1160: LOAD DATA LOCAL INFILE with schemarouter
add_test_executable_ex(NAME sharding_load_data SOURCE sharding_load_data.cc  CONFIG sharding.cnf
        VMS repl_backend LABELS schemarouter)

# MXS-2793: Schemarouter multi-table joins
add_test_executable_ex(NAME mxs2793_multi_table SOURCE mxs2793_multi_table.cc CONFIG mxs2793_multi_table.cnf
        VMS repl_backend LABELS schemarouter)

# MXS-1849: Table family sharding router
# https://jira.mariadb.org/browse/MXS-1849
add_test_executable_ex(NAME mxs1849_table_sharding SOURCE mxs1849_table_sharding.cc
        CONFIG mxs1849_table_sharding.cnf VMS repl_backend LABELS schemarouter)

# MXS-1113: Support of prepared statement for schemarouter
# https://jira.mariadb.org/browse/MXS-1113
add_test_executable_ex(NAME mxs1113_schemarouter_ps SOURCE mxs1113_schemarouter_ps.cc
        CONFIG mxs1113_schemarouter_ps.cnf VMS repl_backend LABELS schemarouter)

# MXS-2631: Duplicate system tables not ignored
add_test_executable_ex(NAME mxs2631_ignore_system_tables SOURCE mxs2631_ignore_system_tables.cc
        CONFIG mxs2631_ignore_system_tables.cnf VMS repl_backend LABELS schemarouter)

# MXS-2972: Caching of shards must take servers into notice
add_test_executable_ex(NAME mxs2972_shard_caching SOURCE mxs2972_shard_caching.cc
        CONFIG mxs2972_shard_caching.cnf VMS repl_backend LABELS schemarouter)

# MXS-4092: Table mapping SQL doesn't use DISTINCT
# https://jira.mariadb.org/browse/MXS-4092
add_test_executable_ex(NAME mxs4092_distinct_tables SOURCE mxs4092_distinct_tables.cc
        CONFIG mxs4092_distinct_tables.cnf VMS repl_backend LABELS schemarouter)

# TODO: Check these all for replication breaking. Fix at the end of test so next test starts faster.

# MXS-1310 Schemarouter implicit database detection
add_test_executable_ex(NAME schemar_implicit_db SOURCE schemar_implicit_db.cpp
        CONFIG schemar_implicit_db.cnf VMS repl_backend LABELS schemarouter)

add_test_executable_ex(NAME schemar_many_connections SOURCE schemar_many_connections.cpp
        CONFIG sharding.cnf VMS repl_backend LABELS schemarouter)

# MXS-1713: SchemaRouter unable to process SHOW DATABASES for a lot of schemas
# https://jira.mariadb.org/browse/MXS-1713
add_test_executable_ex(NAME schemar_lots_of_databases SOURCE mxs1713_lots_of_databases.cpp
        CONFIG mxs1713_lots_of_databases.cnf VMS repl_backend LABELS schemarouter)

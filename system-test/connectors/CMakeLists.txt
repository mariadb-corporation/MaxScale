
# Runs the Connector/R2DBC test suite through MaxScale
add_test_executable_ex(NAME connector_r2dbc SOURCE connector_r2dbc.cc CONFIG connectors.cnf VMS repl_backend LABELS readwritesplit)

# Runs the Connector/J test suite through MaxScale
add_test_executable_ex(NAME connector_j SOURCE connector_j.cc CONFIG connectors.cnf VMS repl_backend LABELS readwritesplit)

# Runs the Connector/NodeJS test suite through MaxScale
add_test_executable_ex(NAME connector_nodejs SOURCE connector_nodejs.cc CONFIG connectors.cnf VMS repl_backend LABELS readwritesplit)

# Runs the Connector/ODBC test suite through MaxScale
add_test_executable_ex(NAME connector_odbc SOURCE connector_odbc.cc CONFIG connectors.cnf VMS repl_backend LABELS readwritesplit)

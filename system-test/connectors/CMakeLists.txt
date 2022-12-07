
# Runs the Connector/R2DBC test suite through MaxScale
add_test_executable_ex(NAME connector_r2dbc SOURCE connector_r2dbc.cc CONFIG connectors.cnf VMS repl_backend LABELS readwritesplit)

# Runs the Connector/J test suite through MaxScale
add_test_executable_ex(NAME connector_j SOURCE connector_j.cc CONFIG connectors.cnf VMS repl_backend LABELS readwritesplit)

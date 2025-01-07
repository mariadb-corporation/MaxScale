# Crash in case of backend node in Galera cluster stopping and then reconnect to Maxscale
add_test_executable_ex(NAME bug676 SOURCE bug676.cc CONFIG standard_galera.cnf VMS galera_backend
        LABELS galeramon)

# Galera node priority test. Also tests MXS-3826 and MXS-3532.
add_test_executable_ex(NAME galera_priority SOURCE galera_priority.cc CONFIG galera_priority.cnf
        VMS galera_backend LABELS galeramon LIGHT)

# INSERT extremelly big number of rows
add_test_executable_ex(NAME lots_of_rows SOURCE lots_of_rows.cc CONFIG standard_galera.cnf
        VMS galera_backend LABELS readwritesplit HEAVY)

# Prepearing and execution statements in the loop
add_test_executable_ex(NAME mxs244_prepared_stmt_loop SOURCE mxs244_prepared_stmt_loop.cc
        CONFIG standard_galera.cnf VMS galera_backend LABELS readwritesplit readconnroute LIGHT)

# Playing with blocking and unblocking nodes under INSERT load
add_test_executable_ex(NAME mxs564_big_dump SOURCE mxs564_big_dump.cc CONFIG galera_mxs564.cnf
        VMS galera_backend LABELS readwritesplit readconnroute)

# MXS-1476: priority value ignored when a Galera node rejoins with a lower wsrep_local_index than current master
# https://jira.mariadb.org/browse/MXS-1476
add_test_executable_ex(NAME mxs1476 SOURCE mxs1476.cc CONFIG mxs1476.cnf VMS galera_backend LABELS galeramon)

# MXS-1751: Maxscale crashes when certain config is in play (with nodes down)
# https://jira.mariadb.org/browse/MXS-1751
add_test_executable_ex(NAME mxs1751_available_when_donor_crash SOURCE mxs1751_available_when_donor_crash.cc
        CONFIG mxs1751_available_when_donor_crash.cnf VMS galera_backend LABELS galeramon)

# MXS-1585: Crash in MaxScale 2.1.12
# https://jira.mariadb.org/browse/MXS-1585
add_test_executable_ex(NAME mxs1585 SOURCE mxs1585.cc CONFIG mxs1585.cnf VMS galera_backend)

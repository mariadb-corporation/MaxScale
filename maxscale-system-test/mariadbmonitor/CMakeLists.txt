# MySQL Monitor switchover
add_test_executable_ex(NAME mysqlmon_switchover SOURCE mysqlmon_switchover.cpp CONFIG mysqlmon_switchover.cnf
        VMS repl_backend LABELS switchover)

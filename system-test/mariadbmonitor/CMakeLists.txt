# MySQL Monitor with Multi-master configurations
add_test_executable_ex(NAME mysqlmon_multimaster SOURCE mysqlmon_multimaster.cpp
        CONFIG mysqlmon_multimaster.cnf VMS repl_backend LABELS mysqlmon BREAKS_REPL)

# MySQL Monitor with Multi-master configurations (assume_unique_hostnames=OFF)
add_test_executable_ex(NAME mysqlmon_multimaster_serverid ORIG_NAME mysqlmon_multimaster
        CONFIG mysqlmon_multimaster_serverid.cnf VMS repl_backend LABELS mysqlmon BREAKS_REPL)

# MySQL Monitor Failover Test
add_test_executable_ex(NAME mysqlmon_detect_standalone_master SOURCE mysqlmon_detect_standalone_master.cpp
        CONFIG mysqlmon_detect_standalone_master.cnf VMS repl_backend LABELS mysqlmon)

# MySQL Monitor Failover (automatic) Test
add_test_executable_ex(NAME mysqlmon_failover_auto SOURCE mysqlmon_failover_auto.cpp
        CONFIG mysqlmon_failover_auto.cnf VMS repl_backend LABELS mysqlmon)

# MySQL Monitor Failover (manual) Test
add_test_executable_ex(NAME mysqlmon_failover_manual SOURCE mysqlmon_failover_manual.cpp
        CONFIG mysqlmon_failover_manual.cnf VMS repl_backend LABELS mysqlmon)

# MySQL Monitor manual failover with many valid candidates
add_test_executable_ex(NAME mysqlmon_failover_manual2_4 SOURCE mysqlmon_failover_manual2.cpp
        CONFIG mysqlmon_failover_manual2_4.cnf VMS repl_backend LABELS mysqlmon)
add_test_executable_ex(NAME mysqlmon_failover_manual2_3 SOURCE mysqlmon_failover_manual2.cpp
        CONFIG mysqlmon_failover_manual2_3.cnf VMS repl_backend LABELS mysqlmon)
add_test_executable_ex(NAME mysqlmon_failover_manual2_2 SOURCE mysqlmon_failover_manual2.cpp
        CONFIG mysqlmon_failover_manual2_2.cnf VMS repl_backend LABELS mysqlmon)

# MySQL Monitor switchover
add_test_executable_ex(NAME mysqlmon_switchover SOURCE mysqlmon_switchover.cpp CONFIG mysqlmon_switchover.cnf
        VMS repl_backend LABELS mysqlmon switchover)

# MySQL Monitor switchover with bad master
add_test_executable_ex(NAME mysqlmon_switchover_bad_master SOURCE mysqlmon_switchover_bad_master.cpp
        CONFIG mysqlmon_switchover_bad_master.cnf VMS repl_backend LABELS mysqlmon switchover)

# MySQL Monitor manual failover with no valid slaves, uses config of mysqlmon_failover_auto. Also MXS-2652.
add_test_executable_ex(NAME mysqlmon_failover_no_slaves SOURCE mysqlmon_failover_no_slaves.cpp
        CONFIG mysqlmon_failover_auto.cnf VMS repl_backend LABELS mysqlmon failover)

# MySQL Monitor Rejoin (good) Test
add_test_executable_ex(NAME mysqlmon_rejoin_good SOURCE mysqlmon_rejoin_good.cpp
        CONFIG mysqlmon_rejoin_good.cnf VMS repl_backend LABELS mysqlmon)

# MySQL Monitor Rejoin (bad) Test, use template for Rejoin (good)
add_test_executable_ex(NAME mysqlmon_rejoin_bad SOURCE mysqlmon_rejoin_bad.cpp CONFIG mysqlmon_rejoin_good.cnf
        VMS repl_backend LABELS mysqlmon)

# MySQL Monitor Rejoin (bad2) Test, use template for Rejoin (good)
add_test_executable_ex(NAME mysqlmon_rejoin_bad2 SOURCE mysqlmon_rejoin_bad2.cpp CONFIG mysqlmon_rejoin_good.cnf
        VMS repl_backend LABELS mysqlmon)

# MySQL Monitor Rejoin tests
add_test_executable_ex(NAME mysqlmon_rejoin_manual SOURCE mysqlmon_rejoin_manual.cpp CONFIG mysqlmon_rejoin_manual.cnf
        VMS repl_backend LABELS mysqlmon)
add_test_executable_ex(NAME mysqlmon_rejoin_manual2 SOURCE mysqlmon_rejoin_manual2.cpp CONFIG mysqlmon_rejoin_manual.cnf
        VMS repl_backend LABELS mysqlmon)

# MySQL Monitor rolling master
add_test_executable_ex(NAME mysqlmon_failover_rolling_master SOURCE mysqlmon_failover_rolling_master.cpp
        CONFIG mysqlmon_failover_rolling_master.cnf VMS repl_backend LABELS mysqlmon)

# MySQL Monitor rejoin old slave
add_test_executable_ex(NAME mysqlmon_failover_rejoin_old_slave SOURCE mysqlmon_failover_rejoin_old_slave.cpp
        CONFIG mysqlmon_failover_rejoin_old_slave.cnf VMS repl_backend LABELS mysqlmon)

# MySQL Monitor rolling restart slaves
add_test_executable_ex(NAME mysqlmon_failover_rolling_restart_slaves SOURCE mysqlmon_failover_rolling_restart_slaves.cpp
        CONFIG mysqlmon_failover_rolling_restart_slaves.cnf VMS repl_backend LABELS mysqlmon)

# MySQL Monitor failover stress
add_test_executable_ex(NAME mysqlmon_failover_stress SOURCE mysqlmon_failover_stress.cpp
        CONFIG mysqlmon_failover_stress.cnf VMS repl_backend LABELS mysqlmon)

# MySQL Monitor switchover stress
add_test_executable_ex(NAME mysqlmon_switchover_stress SOURCE mysqlmon_switchover_stress.cpp
        CONFIG mysqlmon_switchover_stress.cnf VMS repl_backend LABELS mysqlmon)

# Check monitoring and failover when ignore_external_masters is enabled
add_test_executable_ex(NAME mysqlmon_external_master SOURCE mysqlmon_external_master.cpp
        CONFIG mysqlmon_external_master.cnf VMS repl_backend LABELS mysqlmon)

# Check failover, switchover and rejoin with scheduled server events
# Also, MXS-3158
add_test_executable_ex(NAME mysqlmon_fail_switch_events SOURCE mysqlmon_fail_switch_events.cpp
        CONFIG mysqlmon_rejoin_good.cnf VMS repl_backend LABELS mysqlmon)

# MySQL Monitor reset-replication test, use template for Rejoin (good)
add_test_executable_ex(NAME mysqlmon_reset_replication SOURCE mysqlmon_reset_replication.cpp
        CONFIG mysqlmon_rejoin_good.cnf VMS repl_backend LABELS mysqlmon)

# MySQL Monitor low disk switchover
add_test_executable_ex(NAME mysqlmon_switchover_auto SOURCE mysqlmon_switchover_auto.cpp
        CONFIG mysqlmon_switchover_auto.cnf VMS repl_backend LABELS mysqlmon)

# MySQL Monitor series of failovers and rejoins
add_test_executable_ex(NAME mysqlmon_failover_readonly SOURCE mysqlmon_failover_readonly.cpp
        CONFIG mysqlmon_failover_readonly.cnf VMS repl_backend LABELS mysqlmon)

# MXS-1493: Use replication heartbeat in mysqlmon https://jira.mariadb.org/browse/MXS-1493
add_test_executable_ex(NAME verify_master_failure SOURCE verify_master_failure.cpp
        CONFIG verify_master_failure.cnf VMS repl_backend LABELS mysqlmon)

# MariaDB-Monitor enforce_simple_topology
add_test_executable_ex(NAME mysqlmon_enforce_simple SOURCE mysqlmon_enforce_simple.cpp
        CONFIG mysqlmon_enforce_simple.cnf VMS repl_backend LABELS mysqlmon)

# MariaDB-Monitor multimaster failover/switchover
add_test_executable_ex(NAME mysqlmon_fail_switch_multi SOURCE mysqlmon_fail_switch_multi.cpp
        CONFIG mysqlmon_fail_switch_multi.cnf VMS repl_backend LABELS mysqlmon)
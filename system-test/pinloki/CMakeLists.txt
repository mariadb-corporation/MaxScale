# Basic test
add_test_executable_ex(NAME pinloki_basic SOURCE pinloki_basic.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Checks that binlog files are created and indexed correctly
add_test_executable_ex(NAME pinloki_files SOURCE pinloki_files.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# PURGE BINARY LOGS test
add_test_executable_ex(NAME pinloki_purge SOURCE pinloki_purge.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# GTID position test
add_test_executable_ex(NAME pinloki_gtid SOURCE pinloki_gtid.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Restart MaxScale during replication
add_test_executable_ex(NAME pinloki_restart SOURCE pinloki_restart.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Do STOP SLAVE; START SLAVE; while new events are being generated
add_test_executable_ex(NAME pinloki_start_stop SOURCE pinloki_start_stop.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Promote a slave to a master and demote the master to a slave
add_test_executable_ex(NAME pinloki_change_master SOURCE pinloki_change_master.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Replicate large events (larger than 16MB)
add_test_executable_ex(NAME pinloki_large_event SOURCE pinloki_large_event.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Test automatic master selection
add_test_executable_ex(NAME pinloki_select_master SOURCE pinloki_select_master.cc
  CONFIG pinloki_select_master.cnf VMS repl_backend LABELS pinloki)

# TLS for master replication
add_test_executable_ex(NAME pinloki_tls SOURCE pinloki_tls.cc
  CONFIG pinloki.cnf VMS repl_backend backend_ssl LABELS pinloki)

# Test switchover
add_test_executable_ex(NAME pinloki_switchover SOURCE pinloki_switchover.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Test a likely upgrade scenario
add_test_executable_ex(NAME pinloki_upgrade SOURCE pinloki_upgrade.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Test pinloki with galera
#add_test_executable_ex(NAME pinloki_galera SOURCE pinloki_galera.cc
#  CONFIG pinloki_galera.cnf VMS repl_backend LABELS pinloki)

add_test_executable(pinloki_galera.cc pinloki_galera pinloki_galera LABELS REPL_BACKEND GALERA_BACKEND)

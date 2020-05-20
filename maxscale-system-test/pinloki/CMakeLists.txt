# Basic test
add_test_executable_ex(NAME pinloki_basic SOURCE pinloki_basic.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki BREAKS_REPL)

# Checks that binlog files are created and indexed correctly
add_test_executable_ex(NAME pinloki_files SOURCE pinloki_files.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki BREAKS_REPL)

# PURGE BINARY LOGS test
add_test_executable_ex(NAME pinloki_purge SOURCE pinloki_purge.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki BREAKS_REPL)

# GTID position test
add_test_executable_ex(NAME pinloki_gtid SOURCE pinloki_gtid.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki BREAKS_REPL)

# Restart MaxScale during replication
add_test_executable_ex(NAME pinloki_restart SOURCE pinloki_restart.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki BREAKS_REPL)

# Promote a slave to a master and demote the master to a slave
add_test_executable_ex(NAME pinloki_change_master SOURCE pinloki_change_master.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki BREAKS_REPL)

# Replicate large events (larger than 16MB)
add_test_executable_ex(NAME pinloki_large_event SOURCE pinloki_large_event.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki BREAKS_REPL)

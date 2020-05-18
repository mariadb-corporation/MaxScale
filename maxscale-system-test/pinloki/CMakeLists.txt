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

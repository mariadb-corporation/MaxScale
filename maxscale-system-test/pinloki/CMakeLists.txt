# Basic test
add_test_executable_ex(NAME pinloki_basic SOURCE pinloki_basic.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki BREAKS_REPL)

# Checks that binlog files are created and indexed correctly
add_test_executable_ex(NAME pinloki_files SOURCE pinloki_files.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki BREAKS_REPL)

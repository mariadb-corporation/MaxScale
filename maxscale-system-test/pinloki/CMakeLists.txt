# Basic test
add_test_executable_ex(NAME pinloki_basic SOURCE pinloki_basic.cc
  CONFIG pinloki_basic.cnf VMS repl_backend LABELS pinloki BREAKS_REPL)

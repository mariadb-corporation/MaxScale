add_test_executable_ex(NAME config_sync SOURCE config_sync.cc
  CONFIG config_sync.cnf config_sync.cnf VMS repl_backend second_maxscale LABELS config_sync)

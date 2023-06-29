add_test_executable_ex(NAME ldi_basics SOURCE ldi_basics.cc CONFIG ldi_basics.cnf
  VMS repl_backend LABELS ldi)

# TODO: Currently this test doesn't actually test the xpand_import code since
# the executable is not available. Once it is and it can be downloaded the same
# test can be run but with xpand_import installed on the MaxScale machine.
add_test_executable_ex(NAME ldi_xpand ORIG_NAME ldi_basics CONFIG ldi_xpand.cnf
  VMS xpand_backend LABELS ldi)

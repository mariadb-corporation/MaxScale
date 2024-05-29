add_subdirectory(rewritefilter)
add_subdirectory(ldi)

# NamedServerFilter basic test. Tests basic functionality + MXS-2074 and MXS-2037.
add_test_executable_ex(NAME namedserverfilter_basics SOURCE namedserverfilter_basics.cc
        CONFIG namedserverfilter_basics.cnf VMS repl_backend LABELS LIGHT)

# MXS-4113: Namedserverfilter does not work with services
add_test_executable_ex(NAME namedserverfilter_services SOURCE namedserverfilter_services.cc
        CONFIG namedserverfilter_services.cnf VMS repl_backend LABELS LIGHT)

# psreuse filter
add_test_executable_ex(NAME test_psreuse SOURCE test_psreuse.cc
  CONFIG test_psreuse.cnf VMS repl_backend LABELS LIGHT)

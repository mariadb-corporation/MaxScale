# NamedServerFilter basic test.
add_test_executable_ex(NAME namedserverfilter_basics SOURCE namedserverfilter_basics.cc
        CONFIG namedserverfilter_basics.cnf VMS repl_backend LABELS LIGHT)

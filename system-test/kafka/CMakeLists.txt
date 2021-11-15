# Basic kafkacdc test case, makes sure that data is properly propagated from MariaDB to Kafka.
add_test_executable_ex(NAME test_kafkacdc SOURCE test_kafkacdc.cpp
  CONFIG test_kafkacdc.cnf VMS repl_backend LABELS kafkacdc)

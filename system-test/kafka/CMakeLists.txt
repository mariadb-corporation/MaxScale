# Basic kafkacdc test case, makes sure that data is properly propagated from MariaDB to Kafka.
add_test_executable_ex(NAME test_kafkacdc SOURCE test_kafkacdc.cc
  CONFIG test_kafkacdc.cnf VMS repl_backend LABELS kafkacdc)

# MXS-1687: KafkaCDC HA
add_test_executable_ex(NAME mxs1687_kafka_ha SOURCE mxs1687_kafka_ha.cc
  CONFIG mxs1687_kafka_ha.cnf VMS repl_backend LABELS kafkacdc)

# MXS-2588: Import data from Kafka into MariaDB
add_test_executable_ex(NAME mxs2588_kafkaimporter SOURCE mxs2588_kafkaimporter.cc
  CONFIG mxs2588_kafkaimporter.cnf VMS repl_backend LABELS kafkaimporter)

# MXS-5954: KafkaCDC doesn't escape db name identifiers resulting in errors
add_test_executable_ex(NAME mxs5954_weird_table_names SOURCE mxs5954_weird_table_names.cc
  CONFIG mxs5954_weird_table_names.cnf VMS repl_backend LABELS kafkacdc)

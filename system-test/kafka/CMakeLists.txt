# Basic kafkacdc test case, makes sure that data is properly propagated from MariaDB to Kafka.
add_test_executable_ex(NAME test_kafkacdc SOURCE test_kafkacdc.cpp
  CONFIG test_kafkacdc.cnf VMS repl_backend LABELS kafkacdc)

# MXS-1687: KafkaCDC HA
add_test_executable_ex(NAME mxs1687_kafka_ha SOURCE mxs1687_kafka_ha.cpp
  CONFIG mxs1687_kafka_ha.cnf VMS repl_backend LABELS kafkacdc)

# MXS-2588: Import data from Kafka into MariaDB
add_test_executable_ex(NAME mxs2588_kafkaimporter SOURCE mxs2588_kafkaimporter.cc
  CONFIG mxs2588_kafkaimporter.cnf VMS repl_backend LABELS kafkaimporter)

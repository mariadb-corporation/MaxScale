add_test_executable_ex(NAME xpand_basics SOURCE xpand_basics.cc
        CONFIG xpand_basics.cnf VMS xpand_backend LABELS xpand)
set_tests_properties(xpand_basics PROPERTIES TIMEOUT 7200)

# Automatic backend_ssl configuration does not work with Xpand because it does not support the user
# generation option "require ssl". Using a manually generated config file.
add_test_executable_ex(NAME xpand_basics_ssl ORIG_NAME xpand_basics
        CONFIG xpand_basics_ssl.cnf VMS xpand_backend LABELS xpand LISTENER_SSL)

add_test_executable_ex(NAME xpand_transaction_replay SOURCE xpand_transaction_replay.cc
        CONFIG xpand_transaction_replay.cnf VMS xpand_backend LABELS xpand)
set_tests_properties(xpand_transaction_replay PROPERTIES TIMEOUT 7200)

add_test_executable_ex(NAME xpand_distribution SOURCE xpand_distribution.cc
        CONFIG xpand_distribution.cnf VMS xpand_backend LABELS xpand)

add_test_executable_ex(NAME xpand_alter_cluster SOURCE xpand_alter_cluster.cc
        CONFIG xpand_alter_cluster.cnf VMS xpand_backend LABELS xpand)
add_test_executable_ex(NAME xpand_repl SOURCE xpand_repl.cc CONFIG xpand_basics.cnf VMS xpand_backend
        LABELS xpand)

# MXS-3934, MXS-3938
add_test_executable_ex(NAME xpand_runtime_config SOURCE xpand_runtime_config.cc
        CONFIG xpand_runtime_config.cnf VMS xpand_backend LABELS xpand)

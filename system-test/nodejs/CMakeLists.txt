add_test_executable_ex(NAME mxs5527_causal_reads_insert_returning SOURCE mxs5527_causal_reads_insert_returning.cc
        CONFIG mxs5527_causal_reads_insert_returning.cnf VMS repl_backend LABELS readwritesplit)

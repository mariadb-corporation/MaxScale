# MariaDB-to-MariaDB ETL
add_test_executable_ex(NAME etl_mariadb SOURCE etl_mariadb.cc CONFIG etl.cnf VMS repl_backend LABELS etl)

# PostgreSQL-to-MariaDB ETL
add_test_executable_ex(NAME etl_postgresql SOURCE etl_postgresql.cc CONFIG etl.cnf VMS repl_backend LABELS etl)

# Generic ETL
add_test_executable_ex(NAME etl_generic SOURCE etl_generic.cc CONFIG etl.cnf VMS repl_backend LABELS etl)

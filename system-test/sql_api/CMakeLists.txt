# MariaDB-to-MariaDB ETL
add_test_executable_ex(NAME etl_mariadb SOURCE etl_mariadb.cc CONFIG etl.cnf VMS repl_backend LABELS etl)

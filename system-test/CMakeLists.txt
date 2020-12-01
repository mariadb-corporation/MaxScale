# Building test package:
#
#     apt-get install libssl-dev libmariadbclient-dev php5 perl \
#         coreutils realpath libjansson-dev openjdk-7-jdk
#     pip install JayDeBeApi


# Backend labes:
#     REPL_BACKEND
#     GALERA_BACKEND
#     EXTERN_BACKEND
#     CS_BACKEND
#     BREAKS_REPL
#     BREAKS_GALERA
set(CTEST_BUILD_NAME "${BUILDNAME}")
set(TEST_DEFINITIONS "" CACHE INTERNAL "")

# utilities.cmake contains all helper functions and extra tools
include(utilities.cmake)

include_directories(maxtest/include)
# Include the CDC connector headers
include_directories(${CMAKE_SOURCE_DIR}/connectors/cdc-connector)

# Tool used to check backend state
add_test_executable_notest(check_backend.cpp check_backend check_backend LABELS CONFIG REPL_BACKEND GALERA_BACKEND SECOND_MAXSCALE)

# Configuration tests
add_template_manual(bug359 bug359)
add_template_manual(bug495 bug495)
add_template_manual(bug526 bug526)
add_template_manual(bug479 bug479)
add_template_manual(bug493 bug493)
add_template_manual(mxs652_bad_ssl bad_ssl)
add_template_manual(mxs710_bad_socket mxs710_bad_socket)
add_template_manual(mxs710_bad_socket mxs711_two_ports)
add_template_manual(mxs720_line_with_no_equal mxs720_line_with_no_equal)
add_template_manual(mxs720_wierd_line mxs720_wierd_line)
add_template_manual(mxs710_bad_socket mxs799)
add_test_executable(config_test.cpp config_test replication LABELS LIGHT CONFIG)

add_subdirectory(cdc_datatypes)

############################################
# BEGIN: Tests that require GTID           #
############################################

# Configures 'keepalived' on two Maxscale machines and tried failover
add_test_executable(cooperative_monitoring.cpp cooperative_monitoring cooperative_monitoring LABELS REPL_BACKEND SECOND_MAXSCALE)

add_subdirectory(mariadbmonitor)
add_subdirectory(pinloki)

# MXS-1506: Delayed query retry
# https://jira.mariadb.org/browse/MXS-1506
add_test_executable(mxs1506_delayed_retry.cpp mxs1506_delayed_retry mxs1506_delayed_retry LABELS readwritesplit REPL_BACKEND)

# MXS-1506: Delayed retry without master
# https://jira.mariadb.org/browse/MXS-1506
add_test_executable(mxs1506_no_master.cpp mxs1506_no_master mxs1506_delayed_retry LABELS readwritesplit REPL_BACKEND)

# MXS-1507: Transaction replay
# https://jira.mariadb.org/browse/MXS-1507
add_test_executable(mxs1507_trx_replay.cpp mxs1507_trx_replay mxs1507_trx_replay LABELS readwritesplit REPL_BACKEND)

# MXS-1507: Inconsistent transactions
# https://jira.mariadb.org/browse/MXS-1507
add_test_executable(mxs1507_inconsistent_trx.cpp mxs1507_inconsistent_trx mxs1507_trx_replay LABELS readwritesplit REPL_BACKEND)

# MXS-1507: Transaction migration
# https://jira.mariadb.org/browse/MXS-1507
add_test_executable(mxs1507_migrate_trx.cpp mxs1507_migrate_trx mxs1507_trx_replay LABELS readwritesplit REPL_BACKEND)

# MXS-1507: Transaction replay
# https://jira.mariadb.org/browse/MXS-1507
add_test_executable(mxs1507_trx_stress.cpp mxs1507_trx_stress mxs1507_trx_stress LABELS readwritesplit REPL_BACKEND)

# MXS-2187: Multiple transaction replays
# https://jira.mariadb.org/browse/MXS-1507
add_test_executable(mxs2187_multi_replay.cpp mxs2187_multi_replay mxs2187_multi_replay LABELS readwritesplit REPL_BACKEND)

# MXS-1778: Use GTID from OK packets for consistent reads
# https://jira.mariadb.org/browse/MXS-1776
add_test_executable(mxs1778_causal_reads.cpp mxs1778_causal_reads mxs1778_causal_reads LABELS readwritesplit REPL_BACKEND)

# MXS-2443: Test causal_reads=global
# https://jira.mariadb.org/browse/MXS-2443
add_test_executable(mxs2443_global_causal_reads.cpp mxs2443_global_causal_reads mxs2443_global_causal_reads LABELS readwritesplit REPL_BACKEND)

# MXS-1720: Test causal_reads=fast
# https://jira.mariadb.org/browse/MXS-1720
add_test_executable(mxs1720_fast_causal_reads.cpp mxs1720_fast_causal_reads mxs1720_fast_causal_reads LABELS readwritesplit REPL_BACKEND)

# MXS-1961: Standalone master loses master status
# https://jira.mariadb.org/browse/MXS-1961
add_test_executable(mxs1961_standalone_rejoin.cpp mxs1961_standalone_rejoin mxs1961_standalone_rejoin LABELS REPL_BACKEND)

# MXS-2456: Cap transaction replay attempts
add_test_executable(mxs2456_trx_replay_cap.cpp mxs2456_trx_replay_cap mxs2456_trx_replay_cap LABELS readwritesplit REPL_BACKEND)

# MXS-2512: Enable transaction replay for additional rollback errors.
add_test_executable(mxs2512_trx_replay_rollback.cpp mxs2512_trx_replay_rollback mxs2512_trx_replay_rollback LABELS readwritesplit REPL_BACKEND)

# MXS-2785: Binlogfilter database rewrite test
add_test_executable(mxs2785_binlogfilter_rewrite.cpp mxs2785_binlogfilter_rewrite mxs2785_binlogfilter_rewrite LABELS binlogfilter REPL_BACKEND)

# KafkaCDC tests
add_subdirectory(kafka)

############################################
# END: Tests that require GTID             #
############################################

############################################
# BEGIN: Normal tests                      #
############################################

# Routing sanity check. Also acts as the regression test case the following issues:
#
# MXS-127
# MXS-47
# MXS-682
# MXS-957
# MXS-1786
# MXS-3229
add_test_executable(sanity_check.cpp sanity_check replication LABELS readwritesplit LIGHT REPL_BACKEND)

# Repeatedly connect to maxscale while the backends reject all connections, expect no crash
add_test_executable(backend_auth_fail.cpp backend_auth_fail replication LABELS readconnroute REPL_BACKEND)

# Regression case for the bug "MaxScale ignores host in user authentication"
add_test_executable(auth_hostname.cpp auth_hostname replication LABELS MySQLAuth REPL_BACKEND)

# Test wildcard in user host name (user@127.%.%.%). Also test MXS-3172 escaped wildcard in database grant.
add_test_executable(auth_wchost.cpp auth_wchost replication LABELS MySQLAuth LIGHT REPL_BACKEND)

# Regression case for the bug "Routing Hints route to server sometimes doesn't work"
add_test_executable(rhint_basic.cpp rhint_basic rhint_basic LABELS readwritesplit hintfilter REPL_BACKEND)

# Regression case for the bugs "malformed hints cause crash"
add_test_executable(rhint_crash.cpp rhint_crash hints LABELS readwritesplit hintfilter REPL_BACKEND)

# Regression case for the bug "The end comment tag in hints isn't properly detected"
add_test_executable(rhint_comment.cpp rhint_comment hints LABELS readwritesplit hintfilter REPL_BACKEND)

# MaxGUI test
add_test_executable(test_maxgui.cc test_maxgui test_maxgui LABELS maxgui REPL_BACKEND)

# Checks "SELECT * INTO OUTFILE" and "LOAD DATA LOCAL INFILE"
add_test_executable(rwsplit_infile_outfile.cpp rwsplit_infile_outfile replication LABELS readwritesplit REPL_BACKEND)

# Regression case for the bug "'Current no. of conns' not going down"
add_test_executable(router_conn_num.cpp router_conn_num replication LABELS readwritesplit readconnroute maxscale REPL_BACKEND)

# Regression: Crash if no slave available
add_test_executable(router_slaves_blocked.cpp router_slaves_blocked replication LABELS readwritesplit REPL_BACKEND)

# Regression case for the bug ""Different error messages from MariaDB and Maxscale"
add_test_executable(error_messages.cpp error_messages replication LABELS MySQLAuth REPL_BACKEND)

# Regression case for the bug "Wrong error message for Access denied error"
add_test_script(bug562.sh bug562.sh replication LABELS MySQLAuth REPL_BACKEND)

# Regression case for the bug "Wrong charset settings"
add_test_script(bug564.sh bug564.sh replication LABELS MySQLProtocol REPL_BACKEND)

# Regression case for the bug "Clients CLIENT_FOUND_ROWS setting is ignored by maxscale"
add_test_executable(mariadb_client_found_rows.cpp mariadb_client_found_rows replication LABELS MySQLProtocol REPL_BACKEND)

# Regression case for the bug "Crash if files from /dev/shm/ removed"
add_test_script(bug567.sh bug567.sh bug567 LABELS maxscale REPL_BACKEND)

# Regression case for the bug "Using regex filter hangs MaxScale"
add_test_executable(regexf_replace.cpp regexf_replace regexf_replace LABELS regexfilter REPL_BACKEND)

# Attempt to use GRANT with wrong IP, expect no crash or hangs
add_test_executable(auth_fail_crash.cpp auth_fail_crash replication LABELS readwritesplit REPL_BACKEND)

# Regression cases for the bug "Hint filter don't work if listed before regex filter in configuration file"
# (different filter sequence and configuration, but the same test, see .cnf for details)
add_test_derived(bug585 bug587 bug585 LABELS regexfilter REPL_BACKEND)
add_test_executable(bug587.cpp bug587 bug587 LABELS regexfilter hintfilter REPL_BACKEND)
add_test_derived(bug587_1 bug587 bug587_1 LABELS regexfilter hintfilter REPL_BACKEND)

# Tries to connect Maxscale when all slaves stopped
add_test_executable(auth_users_from_master.cpp auth_users_from_master replication LABELS MySQLAuth readwritesplit REPL_BACKEND)

# Tries to do change user in the loop, checks that autorization is still ok
add_test_executable(auth_change_user_loop.cpp auth_change_user_loop auth_change_user_loop LABELS MySQLAuth MySQLProtocol REPL_BACKEND)

# Simple test with enable_root_user=true
add_test_executable(auth_enable_root.cpp auth_enable_root auth_enable_root LABELS MySQLAuth MySQLProtocol REPL_BACKEND)

# Regression case for the bug "Crash when user define with old password style (before 4.1 protocol)"
add_test_executable(auth_old_pw.cpp auth_old_pw replication LABELS MySQLAuth MySQLProtocol REPL_BACKEND)

# Crash when host name for some user in mysql.user is very long
add_test_executable(auth_long_hostname.cpp auth_long_hostname replication LABELS MySQLAuth MySQLProtocol REPL_BACKEND)

# Block backends (master or all slaves) and tries to connect Maxscale
add_test_executable(no_backends_crash.cpp no_backends_crash replication LABELS readwritesplit readconnroute maxscale REPL_BACKEND)

# Block all backends
add_test_executable(startup_fail_no_servers.cpp startup_fail_no_servers startup_fail_no_servers LABELS readwritesplit readconnroute maxscale REPL_BACKEND)

# Bad TEE filter configuration
add_test_executable(teef_no_master.cpp teef_no_master teef_no_master LABELS MySQLAuth MySQLProtocol REPL_BACKEND)

# Wrong processing of 'SET GLOBAL sql_mode="ANSI"'
add_test_executable(auth_sqlmode_ansi.cpp auth_sqlmode_ansi auth_sqlmode_ansi LABELS MySQLAuth REPL_BACKEND)

# Prepared statement from PHP application
add_test_executable(rwsplit_php_stmt.cpp rwsplit_php_stmt replication LABELS readwritesplit LIGHT REPL_BACKEND)

# Regression case for the bug "Regex filter and shorter than original replacement queries MaxScale" (crash)
add_test_executable(regexf_replace_crash.cpp regexf_replace_crash regexf_replace_crash LABELS regexfilter REPL_BACKEND)

# Test MariaDB 10.2 bulk inserts
add_test_executable(bulk_insert.cpp bulk_insert bulk_insert LABELS MySQLProtocol REPL_BACKEND 10.2)

# Tests for the CCRFilter module
add_test_executable(ccrfilter.cpp ccrfilter_test ccrfilter LABELS ccrfilter LIGHT REPL_BACKEND)
add_test_executable(ccrfilter_global.cpp ccrfilter_global_test ccrfilter_global LABELS ccrfilter LIGHT REPL_BACKEND)

# Tries to reconfigure replication setup to use another node as a Master
add_test_executable(change_master_during_session.cpp change_master_during_session replication LABELS readwritesplit mysqlmon REPL_BACKEND)

# Executes change_user command in the loop
add_test_executable(change_user.cpp change_user replication LABELS MySQLAuth readwritesplit MySQLProtocol LIGHT REPL_BACKEND)

# Tries to connect to non existing DB, expects no crash
add_test_executable(connect_to_nonexisting_db.cpp connect_to_nonexisting_db replication LABELS MySQLAuth MySQLProtoco LIGHT REPL_BACKEND)

# check if max_connections parameter works
add_test_executable(connection_limit.cpp connection_limit connection_limit LABELS maxscale LIGHT REPL_BACKEND)

# Tries to open to many connections, expect no crash
add_test_executable(crash_out_of_files.cpp crash_out_of_files load LABELS maxscale HEAVY REPL_BACKEND)

# Tries INSERTs with size close to 0x0ffffff * N
add_test_executable(different_size_rwsplit.cpp different_size_rwsplit replication LABELS readwritesplit HEAVY REPL_BACKEND)

# Tries to use 'maxkeys', 'maxpasswrd'
add_test_executable(encrypted_passwords.cpp encrypted_passwords replication LABELS maxscale LIGHT REPL_BACKEND)

# Basic MaxCtrl test
add_test_executable(maxctrl_basic.cpp maxctrl_basic maxctrl_basic LABELS maxctrl REPL_BACKEND)

# MXS-2167: Monitors should be able to use extra_port
add_test_executable(mxs2167_extra_port.cpp mxs2167_extra_port mxs2167_extra_port LABELS REPL_BACKEND)

# Test KILL QUERY functionality
add_test_executable(kill_query.cpp kill_query kill_query LABELS REPL_BACKEND)

# MXS-2250: DESCRIBE on temporary table should work.
add_test_executable(mxs2250_describe_temp_table.cpp mxs2250_describe_temp_table mxs2250_describe_temp_table LABELS REPL_BACKEND)

# MXS-2355: Try to log in using a different authentication plugin
add_test_executable(mxs2355_wrong_auth.cpp mxs2355_wrong_auth maxctrl LABELS REPL_BACKEND)

# Test monitor state change events when manually clearing server bits
add_test_executable(false_monitor_state_change.cpp false_monitor_state_change replication LABELS mysqlmon REPL_BACKEND)

# A set of tests for Firewall filter
add_test_executable(fwf.cpp fwf fwf LABELS dbfwfilter REPL_BACKEND)
add_test_executable(fwf2.cpp fwf2 fwf LABELS dbfwfilter REPL_BACKEND)
add_test_executable(fwf_duplicate_rules.cpp fwf_duplicate_rules fwf LABELS dbfwfilter REPL_BACKEND)
add_test_executable(fwf_prepared_stmt.cpp fwf_prepared_stmt fwf LABELS dbfwfilter REPL_BACKEND)
add_test_executable(fwf_actions.cpp fwf_actions fwf_action LABELS dbfwfilter REPL_BACKEND)
add_test_executable(fwf_logging.cpp fwf_logging fwf_logging LABELS dbfwfilter REPL_BACKEND)
add_test_executable(fwf_reload.cpp fwf_reload fwf LABELS dbfwfilter REPL_BACKEND)
add_test_executable(fwf_syntax.cpp fwf_syntax fwf_syntax LABELS dbfwfilter REPL_BACKEND)
add_test_executable(fwf_com_ping.cpp fwf_com_ping fwf_com_ping LABELS dbfwfilter REPL_BACKEND)
add_test_executable(fwf_strict.cpp fwf_strict fwf_strict LABELS dbfwfilter REPL_BACKEND)
add_test_executable(mxs1583_fwf.cpp mxs1583_fwf mxs1583_fwf LABELS dbfwfilter REPL_BACKEND)

# Block and unblock Master and check that Maxscale survived
add_test_executable(kill_master.cpp kill_master replication LABELS readwritesplit LIGHT REPL_BACKEND)

# Test insertstream filter
add_test_script(insertstream.sh insertstream.sh insertstream LABELS insertstream REPL_BACKEND)

# Check load balancing
add_test_executable(load_balancing.cpp load_balancing load LABELS readwritesplit LIGHT REPL_BACKEND)

# Check load balancing parameters with 10 persistent connections
add_test_derived(load_balancing_pers10 load_balancing load_pers10 LABELS readwritesplit HEAVY REPL_BACKEND)

# Test with extremely big blob inserting
add_test_executable(longblob.cpp longblob longblob LABELS readwritesplit readconnroute UNSTABLE HEAVY REPL_BACKEND)

# Schemarouter implicit database detection
add_test_executable(mxs1310_implicit_db.cpp mxs1310_implicit_db mxs1310_implicit_db LABELS schemarouter REPL_BACKEND)

# Retry reads with persistent connections
add_test_executable(mxs1323_retry_read.cpp mxs1323_retry_read mxs1323 LABELS readwritesplit LIGHT REPL_BACKEND)
add_test_executable(mxs1323_stress.cpp mxs1323_stress mxs1323 LABELS readwritesplit REPL_BACKEND)

# A set of MariaDB server tests executed against Maxscale RWSplit
add_test_script(mariadb_tests_hartmut.sh mariadb_tests_hartmut.sh replication LABELS readwritesplit REPL_BACKEND)

# Creates a number of connections > max_connections setting
add_test_executable(max_connections.cpp max_connections replication LABELS MySQLAuth MySQLProtocol UNSTABLE HEAVY REPL_BACKEND)

# Checks tha Maxscale processis running as 'maxscale' user
add_test_executable(maxscale_process_user.cpp maxscale_process_user replication LABELS maxscale LIGHT REPL_BACKEND)

# Test of multi master monitor
add_test_executable(mm.cpp mm mm LABELS mysqlmon BREAKS_REPL REPL_BACKEND)

# Regression case for the bug "Two monitors loaded at the same time result into not working installation"
add_test_executable(mxs118.cpp mxs118 mxs118 LABELS maxscale LIGHT REPL_BACKEND)

# Regression case for the bug "SELECT INTO OUTFILE query succeeds even if backed fails"
add_test_executable(mxs280_select_outfile.cpp mxs280_select_outfile replication LABELS readwritesplit REPL_BACKEND)

# Testing of the master replacement feature
add_test_executable(mxs359_master_switch.cpp mxs359_master_switch mxs359_master_switch LABELS readwritesplit REPL_BACKEND)

# Test read-only connections with master replacement
add_test_executable(mxs359_read_only.cpp mxs359_read_only mxs359_read_only LABELS readwritesplit REPL_BACKEND)

# Test master_failure_mode=error_on_write and master replacement
add_test_executable(mxs359_error_on_write.cpp mxs359_error_on_write mxs359_error_on_write LABELS readwritesplit REPL_BACKEND)

# Binary protocol prepared statement tests (also tests MXS-2266)
add_test_executable(binary_ps.cpp binary_ps replication LABELS readwritesplit LIGHT REPL_BACKEND)
add_test_executable(binary_ps_cursor.cpp binary_ps_cursor replication LABELS readwritesplit LIGHT REPL_BACKEND)

# Creates and closes a lot of connections, checks that 'list servers' shows 0 connections at the end
add_test_executable(mxs321.cpp mxs321 replication LABELS maxscale readwritesplit REPL_BACKEND)

# Load huge file with 'LOAD DATA LOCAL INFILE'
add_test_executable(mxs365.cpp mxs365 replication LABELS readwritesplit REPL_BACKEND)

# Test users with different level privileges: database, table, column and procedure.
# MXS-37, MXS-3303
add_test_executable(client_privileges.cc client_privileges replication LABELS LIGHT REPL_BACKEND)

# Connect repeatedly to Schema router and execute simple query, check if auth is ok
add_test_executable(mxs431.cpp mxs431 sharding LABELS schemarouter REPL_BACKEND BREAKS_REPL)

# Open connection, execute 'change user', close connection in the loop
add_test_executable(mxs548_short_session_change_user.cpp mxs548_short_session_change_user mxs548 LABELS MySQLProtocol readwritesplit REPL_BACKEND)

# Playing with blocking and unblocking Master under load
add_test_executable(mxs559_block_master.cpp mxs559_block_master mxs559 LABELS readwritesplit REPL_BACKEND)

# Executes simple queries from python script in the loop
add_test_script(mxs585.py mxs585.py replication LABELS readwritesplit readconnroute UNSTABLE HEAVY REPL_BACKEND)

# Simple transactions in the loop from python script with client SSL on
add_test_script(mxs598.py mxs598.py ssl LABELS MySQLProtocol UNSTABLE HEAVY REPL_BACKEND)

# Regression case for the bug "MaxScale fails to start silently if config file is not readable"
add_test_executable(mxs621_unreadable_cnf.cpp mxs621_unreadable_cnf replication LABELS maxscale REPL_BACKEND)

# playing with 'restart service' and restart Maxscale under load
add_test_executable(mxs657_restart.cpp mxs657_restart replication LABELS maxscale HEAVY REPL_BACKEND)
add_test_executable(mxs657_restart_service.cpp mxs657_restart_service replication LABELS maxscale REPL_BACKEND)

# Connect using different default database using user with database and table level grants
add_test_executable(mxs716.cpp mxs716 replication LABELS MySQLAuth LIGHT REPL_BACKEND)

# MaxScale configuration check functionality test (maxscale -c)
add_test_executable(mxs722.cpp mxs722 mxs722 LABELS maxscale LIGHT REPL_BACKEND)

# Simple connect test in bash, checks that defined in cmd line DB is selected
add_test_script(mxs791.sh mxs791.sh replication LABELS UNSTABLE HEAVY REPL_BACKEND)

# Checks "Current no. of conns" output after long blob inserting
add_test_executable(mxs812_1.cpp mxs812_1 longblob LABELS readwritesplit REPL_BACKEND)

# Checks "Current no. of conns" output after long blob inserting
add_test_executable(mxs812_2.cpp mxs812_2 replication LABELS readwritesplit REPL_BACKEND)

# Tests maxpasswd
add_test_executable(mxs822_maxpasswd.cpp mxs822_maxpasswd maxpasswd LABELS maxscale REPL_BACKEND)

# Do only SELECTS during time > wait_timeout and then do INSERT
add_test_executable(mxs827_write_timeout.cpp mxs827_write_timeout mxs827_write_timeout LABELS readwritesplit REPL_BACKEND)

# MXS-872: MaxScale doesn't understand roles
# https://jira.mariadb.org/browse/MXS-872
add_test_executable(mxs872_roles.cpp mxs872_roles replication LABELS REPL_BACKEND)

# MXS-1947: Composite roles are not supported
# https://jira.mariadb.org/browse/MXS-1947
add_test_executable(mxs1947_composite_roles.cpp mxs1947_composite_roles replication LABELS REPL_BACKEND)

# Block and unblock first and second slaves and check that they are recovered
add_test_executable(mxs874_slave_recovery.cpp mxs874_slave_recovery mxs874 LABELS readwritesplit REPL_BACKEND)

# A set of dynamic configuration tests
# Server removal test
add_test_executable(mxs922_bad_server.cpp mxs922_bad_server mxs922 LABELS maxscale REPL_BACKEND)

# Server creation test
add_test_executable(mxs922_server.cpp mxs922_server mxs922_base LABELS maxscale REPL_BACKEND)

# Monitor creation test
add_test_executable(mxs922_monitor.cpp mxs922_monitor mxs922_base LABELS maxscale REPL_BACKEND)

# Double creation of listeners, expect no crash
add_test_executable(mxs922_double_listener.cpp mxs922_double_listener mxs922_base LABELS maxscale REPL_BACKEND)

# Test persisting of configuration changes
add_test_executable(mxs922_restart.cpp mxs922_restart mxs922 LABELS maxscale REPL_BACKEND)

# Server scaling test
add_test_executable(mxs922_scaling.cpp mxs922_scaling mxs922_base LABELS maxscale REPL_BACKEND)

# Dynamic listener SSL test
add_test_executable(mxs922_listener_ssl.cpp mxs922_listener_ssl mxs922_base LABELS maxscale BACKEND_SSL REPL_BACKEND)

# Alter routers at runtime
add_test_executable(alter_router.cpp alter_router alter_router LABELS maxscale REPL_BACKEND)

# Test of MaxRows filter
add_test_executable(mxs1071_maxrows.cpp mxs1071_maxrows maxrows LABELS maxrowsfilter REPL_BACKEND)

# Test of Masking filter
add_test_script(masking_mysqltest masking_mysqltest_driver.sh masking_mysqltest LABELS maskingfilter REPL_BACKEND)

add_test_script(masking_user masking_user.sh masking_mysqltest LABELS maskingfilter REPL_BACKEND)

add_test_executable(masking_auto_firewall.cpp masking_auto_firewall masking_auto_firewall LABELS masking REPL_BACKEND)

# Test of Cache filter - basics
add_test_script(cache_basic cache_basic.sh cache_basic LABELS cachefilter REPL_BACKEND)

# Test of Cache filter - runtime configuration
add_test_executable(cache_runtime.cpp cache_runtime cache_runtime LABELS cachefilter REPL_BACKEND)

# Test of Cache filter - ttl runtime configuration
add_test_executable(cache_runtime_ttl.cpp cache_runtime_ttl cache_runtime_ttl LABELS cachefilter REPL_BACKEND)

# Test of Cache filter - invalidate
add_test_executable(cache_invalidate.cpp cache_invalidate cache_invalidate LABELS cachefilter REPL_BACKEND)

# Test of Cache filter - users
add_test_executable(cache_users.cpp cache_users cache_users LABELS cachefilter REPL_BACKEND)

# Set utf8mb4 in the backend and restart Maxscale
add_test_executable(mxs951_utfmb4.cpp mxs951_utfmb4 replication LABELS REPL_BACKEND)

# Proxy protocol test
add_test_executable(proxy_protocol.cpp proxy_protocol proxy_protocol LABELS MySQLAuth MySQLProtocol REPL_BACKEND)

# Regression case for the bug "Defunct processes after maxscale have executed script during failover"
add_test_executable(mxs1045.cpp mxs1045 mxs1045 LABELS maxscale REPL_BACKEND)

# MXS-1123: connect_timeout setting causes frequent disconnects
# https://jira.mariadb.org/browse/MXS-1123
add_test_executable(mxs1123.cpp mxs1123 mxs1123 LABELS maxscale readwritesplit REPL_BACKEND)

# MXS-1319: Maxscale selecting extra whitespace while loading users
# https://jira.mariadb.org/browse/MXS-1319
add_test_executable(mxs1319.cpp mxs1319 replication LABELS MySQLAuth REPL_BACKEND)

# MXS-1418: Removing a server from a service breaks the connection
# https://jira.mariadb.org/browse/MXS-1418
add_test_executable(mxs1418.cpp mxs1418 replication LABELS readwritesplit maxscale REPL_BACKEND)

# MXS-1295: MaxScale's readwritesplit router does not take into account the fact
# that stored procedure call may change the value of a user variable
#
# https://jira.mariadb.org/browse/MXS-1295
add_test_executable(mxs1295_sp_call.cpp mxs1295_sp_call mxs1295 LABELS maxscale readwritesplit REPL_BACKEND)

# MXS-1457: Deleted servers are not ignored when users are loaded
# https://jira.mariadb.org/browse/MXS-1457
add_test_executable(mxs1457_ignore_deleted.cpp mxs1457_ignore_deleted mxs1457_ignore_deleted LABELS REPL_BACKEND)

# MXS-1468: Using dynamic commands to create readwritesplit configs fail after restart
# https://jira.mariadb.org/browse/MXS-1468
add_test_executable(mxs1468.cpp mxs1468 mxs1468 LABELS REPL_BACKEND)

# MXS-1503: Test master_reconnection
# https://jira.mariadb.org/browse/MXS-1503
add_test_executable(mxs1503_master_reconnection.cpp mxs1503_master_reconnection mxs1503_master_reconnection LABELS readwritesplit REPL_BACKEND)

# Master reconnection with session commands
add_test_executable(mxs1503_queued_sescmd.cpp mxs1503_queued_sescmd mxs1503_master_reconnection LABELS readwritesplit REPL_BACKEND)

# Check that no extra slaves are taken into use
add_test_executable(mxs1503_extra_slaves.cpp mxs1503_extra_slaves mxs1503_extra_slaves LABELS readwritesplit REPL_BACKEND)

# MXS-1509: Show correct server state for multisource replication
# https://jira.mariadb.org/browse/MXS-1509
add_test_executable(mxs1509.cpp mxs1509 mxs1509 LABELS mysqlmon REPL_BACKEND)

# MXS-1516: existing connection don't change routing, even if master switched
# https://jira.mariadb.org/browse/MXS-1516
add_test_executable(mxs1516.cpp mxs1516 replication LABELS readconnroute REPL_BACKEND)

# MXS-1549: Optimistic transaction tests
# https://jira.mariadb.org/browse/MXS-1549
add_test_executable(mxs1549_optimistic_trx.cpp mxs1549_optimistic_trx mxs1549_optimistic_trx LABELS readwritesplit REPL_BACKEND)

# MXS-1643: Too many monitor events are triggered
# https://jira.mariadb.org/browse/MXS-1643
add_test_executable(mxs1643_extra_events.cpp mxs1643_extra_events mxs1643_extra_events LABELS mysqlmon REPL_BACKEND)

# MXS-1653: sysbench failed to initialize w/ MaxScale read/write splitter
# https://jira.mariadb.org/browse/MXS-1653
add_test_executable(mxs1653_ps_hang.cpp mxs1653_ps_hang replication LABELS readwritesplit REPL_BACKEND)

# MXS-1677: "Query could not be parsed" errors
# https://jira.mariadb.org/browse/MXS-1677
add_test_executable(mxs1677_temp_table.cpp mxs1677_temp_table replication LABELS readwritesplit REPL_BACKEND)

# MXS-1678: Stopping IO thread on relay master causes it to be promoted as master
# https://jira.mariadb.org/browse/MXS-1678
add_test_executable(mxs1678_relay_master.cpp mxs1678_relay_master replication LABELS mysqlmon REPL_BACKEND)

# MXS-1713: SchemaRouter unable to process SHOW DATABASES for a lot of schemas
# https://jira.mariadb.org/browse/MXS-1713
add_test_executable(mxs1713_lots_of_databases.cpp mxs1713_lots_of_databases mxs1713_lots_of_databases LABELS schemarouter REPL_BACKEND)

# MXS-1731: Empty version_string is not detected
# https://jira.mariadb.org/browse/MXS-1731
add_test_executable(mxs1731_old_persisted_config.cpp mxs1731_old_persisted_config replication LABELS maxscale REPL_BACKEND)

# MXS-1743: Maxscale unable to enforce round-robin between read service for Slave
# https://jira.mariadb.org/browse/MXS-1743
add_test_executable(mxs1743_rconn_bitmask.cpp mxs1743_rconn_bitmask mxs1743_rconn_bitmask LABELS REPL_BACKEND)

# MXS-1760: With use_sql_variables_in=master, unknown PS errors are logged
# https://jira.mariadb.org/browse/MXS-1760
add_test_executable(mxs1760_use_sql_variables_in.cpp mxs1760_use_sql_variables_in mxs1760_use_sql_variables_in LABELS readwritesplit REPL_BACKEND)

# MXS-1773: Failing LOAD DATA LOCAL INFILE confuses readwritesplit
# https://jira.mariadb.org/browse/MXS-1773
add_test_executable(mxs1773_failing_ldli.cpp mxs1773_failing_ldli replication LABELS readwritesplit REPL_BACKEND)

# MXS-1776: recursive COM_STMT_EXECUTE execution
# https://jira.mariadb.org/browse/MXS-1776
add_test_executable(mxs1776_ps_exec_hang.cpp mxs1776_ps_exec_hang replication LABELS readwritesplit REPL_BACKEND)

# MXS-1787: Crash with PS: CALL p1((SELECT f1()), ?)
# https://jira.mariadb.org/browse/MXS-1787
add_test_executable(mxs1787_call_ps.cpp mxs1787_call_ps replication LABELS readwritesplit REPL_BACKEND)

# Modified test for MXS-1787 that checks that slave reconnection works
add_test_executable(mxs1787_slave_reconnection.cpp mxs1787_slave_reconnection replication LABELS readwritesplit REPL_BACKEND)

# MXS-1804: request 16M-1 stmt_prepare command packet connect hang
# https://jira.mariadb.org/browse/MXS-1804
add_test_executable(mxs1804_long_ps_hang.cpp mxs1804_long_ps_hang replication LABELS readwritesplit REPL_BACKEND)

# MXS-1808: Crash with mysql_stmt_send_long_data
# https://jira.mariadb.org/browse/MXS-1808
add_test_executable(mxs1808_long_data.cpp mxs1808_long_data replication LABELS readwritesplit REPL_BACKEND)

# MXS-1824: Debug assertion with two open cursors
# https://jira.mariadb.org/browse/MXS-1824
add_test_executable(mxs1824_double_cursor.cpp mxs1824_double_cursor replication LABELS readwritesplit REPL_BACKEND)

# MXS-1828: Multiple LOAD DATA LOCAL INFILE commands in one query cause a hang
# https://jira.mariadb.org/browse/MXS-1828
add_test_executable(mxs1828_double_local_infile.cpp mxs1828_double_local_infile replication LABELS readwritesplit REPL_BACKEND)

# MXS-1831: No error on invalid monitor parameter alteration
# https://jira.mariadb.org/browse/MXS-1831
add_test_executable(mxs1831_unknown_param.cpp mxs1831_unknown_param replication LABELS REPL_BACKEND)

# MXS-1873: Large session commands cause errors
# https://jira.mariadb.org/browse/MXS-1873
add_test_executable(mxs1873_large_sescmd.cpp mxs1873_large_sescmd replication LABELS readwritesplit REPL_BACKEND)

# MXS-1896: LOAD DATA INFILE is mistaken for LOAD DATA LOCAL INFILE
# https://jira.mariadb.org/browse/MXS-1896
add_test_executable(mxs1896_load_data_infile.cpp mxs1896_load_data_infile replication LABELS readwritesplit REPL_BACKEND)

# MXS-1899: generated [maxscale] section causes errors
# https://jira.mariadb.org/browse/MXS-1899
add_test_executable(mxs1899_generated_cnf.cpp mxs1899_generated_cnf replication LABELS REPL_BACKEND)

# 'namedserverfilter' test
add_test_executable(namedserverfilter.cpp namedserverfilter_test namedserverfilter LABELS namedserverfilter LIGHT REPL_BACKEND)

# Authentication error testing
add_test_executable(no_password.cpp no_password replication LABELS MySQLAuth LIGHT REPL_BACKEND)

# Open and immediatelly close a big number of connections
add_test_executable(open_close_connections.cpp open_close_connections replication LABELS maxscale readwritesplit REPL_BACKEND)

# Check if prepared statement works via Maxscale (via RWSplit)
add_test_executable(prepared_statement.cpp prepared_statement replication LABELS readwritesplit LIGHT REPL_BACKEND)

# Connect to ReadConn in master mode and check if there is only one backend connection to master
add_test_executable(readconnrouter_master.cpp readconnrouter_master replication LABELS readconnroute LIGHT REPL_BACKEND)

# Creates 100 connections to ReadConn in slave mode and check if connections are distributed among all slaves
add_test_executable(readconnrouter_slave.cpp readconnrouter_slave replication LABELS readconnroute LIGHT REPL_BACKEND)

# Regex filter test
add_test_executable(regexfilter1.cpp regexfilter1 regexfilter1 LABELS regexfilter LIGHT REPL_BACKEND)

# check that Maxscale is reacting correctly on ctrc+c signal and termination does not take ages
add_test_script(run_ctrl_c.sh run_ctrl_c.sh replication LABELS maxscale LIGHT REPL_BACKEND)

# run a set of queries in the loop (see setmix.sql) using Perl client
add_test_script(run_session_hang.sh run_session_hang.sh replication LABELS readwritesplit REPL_BACKEND)

# Checks changes of COM_SELECT and COM_INSERT after queris to check if RWSplit sends queries to master or to slave depending on if it is write or read only query
add_test_executable(rw_select_insert.cpp rw_select_insert replication LABELS readwritesplit REPL_BACKEND)

# Checks connections are distributed equaly among backends
add_test_executable(rwsplit_conn_num.cpp rwsplit_conn_num repl_lgc LABELS readwritesplit LIGHT REPL_BACKEND)

# Check that there is one connection to Master and one connection to one of slaves
add_test_executable(rwsplit_connect.cpp rwsplit_connect replication LABELS readwritesplit LIGHT REPL_BACKEND)

# Test of the read-only mode for readwritesplit when master fails (blocked)
add_test_executable(rwsplit_readonly.cpp rwsplit_readonly rwsplit_readonly LABELS readwritesplit REPL_BACKEND)

# Test of the read-only mode for readwritesplit when master fails (blocked), under load
add_test_executable(rwsplit_readonly_stress.cpp rwsplit_readonly_stress rwsplit_readonly LABELS readwritesplit HEAVY REPL_BACKEND)

# Test readwritesplit multi-statement handling
add_test_executable(rwsplit_multi_stmt.cpp rwsplit_multi_stmt rwsplit_multi_stmt LABELS readwritesplit REPL_BACKEND)

# Schemarouter duplicate database detection test: create DB on all nodes and then try query againt schema router
add_test_executable(schemarouter_duplicate.cpp schemarouter_duplicate schemarouter_duplicate LABELS schemarouter REPL_BACKEND)

# Test 10.3 SEQUENCE objects
add_test_executable(sequence.cpp sequence replication LABELS LIGHT REPL_BACKEND)

# Test 10.1 compound statements
add_test_executable(compound_statement.cpp compound_statement replication LABELS LIGHT REPL_BACKEND)

# test for 'max_sescmd_history' and 'connection_timeout' parameters
add_test_executable(session_limits.cpp session_limits session_limits LABELS readwritesplit REPL_BACKEND)

# Test of schema router
add_test_executable(sharding.cpp sharding sharding LABELS schemarouter BREAKS_REPL REPL_BACKEND)

# MXS-1160: LOAD DATA LOCAL INFILE with schemarouter
add_test_executable(sharding_load_data.cpp sharding_load_data sharding LABELS schemarouter BREAKS_REPL REPL_BACKEND)

# Do short sessions (open conn, short query, close conn) in the loop
add_test_executable(short_sessions.cpp short_sessions replication LABELS readwritesplit readconnroute REPL_BACKEND)

# Do short sessions (open conn, short query, close conn) in the loop, client ssl is ON
add_test_derived(short_sessions_ssl short_sessions ssl LABELS readwritesplit readconnroute BACKEND_SSL REPL_BACKEND)

# Regression case for crash if 'show monitors' command is issued, but no monitor is not running
add_test_executable(show_monitor_crash.cpp show_monitor_crash show_monitor_crash LABELS maxscale REPL_BACKEND)

# Check how Maxscale works in case of one slave failure, only one slave is configured
add_test_executable(slave_failover.cpp slave_failover replication.one_slave LABELS readwritesplit REPL_BACKEND)

# Execute queries of different size, check data is the same when accessing via Maxscale and directly to backend
add_test_executable(sql_queries.cpp sql_queries replication LABELS readwritesplit REPL_BACKEND)

# Execute queries of different size, check data is the same when accessing via Maxscale and directly to backend, one persistant connection configured
add_test_derived(sql_queries_pers1 sql_queries sql_queries_pers1 LABELS maxscale readwritesplit HEAVY REPL_BACKEND)

# Execute queries of different size, check data is the same when accessing via Maxscale and directly to backend, 10 persistant connections configured
add_test_derived(sql_queries_pers10 sql_queries sql_queries_pers10 LABELS maxscale readwritesplit HEAVY REPL_BACKEND)

# Execute queries of different size, check data is the same when accessing via Maxscale and directly to backend, client ssl is ON
add_test_derived(ssl sql_queries ssl LABELS maxscale readwritesplit REPL_BACKEND)

# Testing slaves who have lost their master and how MaxScale works with them
add_test_executable(stale_slaves.cpp stale_slaves stale_slaves LABELS mysqlmon REPL_BACKEND)

# Run sysbech test and block one slave during test execution
add_test_executable(sysbench_kill_slave.cpp sysbench_kill_slave replication LABELS UNSTABLE HEAVY REPL_BACKEND)

# Check temporal tables commands functionality
add_test_executable(temporal_tables.cpp temporal_tables replication LABELS readwritesplit REPL_BACKEND)

# Test routing hints (mainly about how readwritesplit deals with them)
add_test_executable(test_hints.cpp test_hints hints2 LABELS hintfilter readwritesplit LIGHT REPL_BACKEND)

# Run MaxCtrl test suite
# add_test_executable(test_maxctrl.cpp test_maxctrl maxctrl LABELS REPL_BACKEND)


# Creates KDC and tries authrization via GSSAPI (both client and backend)
# works only with yum-based distributions
# TODO: make it working with zypper and apt, move part of KDC setup to MDBCI
add_test_executable(kerberos_setup.cpp kerberos_setup kerberos LABELS HEAVY gssapi REPL_BACKEND)
add_test_derived(kerberos_setup_ssl kerberos_setup kerberos_ssl LABELS HEAVY gssapi BACKEND_SSL REPL_BACKEND)

# Configures 'keepalived' on two Maxscale machines and tried failover
#add_test_executable(keepalived.cpp keepalived keepalived LABELS REPL_BACKEND SECOND_MAXSCALE)

# a tool to delete RDS Aurora cluster
add_test_executable_notest(delete_rds.cpp delete_rds replication LABELS EXTERN_BACKEND)

# a tool to create RDS Aurora cluster
add_test_executable_notest(create_rds.cpp create_rds replication LABELS EXTERN_BACKEND)

# start sysbench against RWSplit for infinite execution
add_test_executable_notest(long_sysbench.cpp long_sysbench replication LABELS readwritesplit REPL_BACKEND)

# own long test
# 'long_test_time' variable defines time of execution (in seconds)
add_test_executable_notest(long_test.cpp long_test replication LABELS readwritesplit REPL_BACKEND)

# test effect of local_address in configuration file
add_test_executable(local_address.cpp local_address local_address LABELS REPL_BACKEND)

# MXS-1628: Security scanner says MaxScale is vulnerable to ancient MySQL vulnerability
# https://jira.mariadb.org/browse/MXS-1628
add_test_executable(mxs1628_bad_handshake.cpp mxs1628_bad_handshake replication LABELS REPL_BACKEND)

# MXS-173 throttling filter
add_test_executable(mxs173_throttle_filter.cpp mxs173_throttle_filter mxs173_throttle_filter LABELS REPL_BACKEND)

 # MXS-1889: A single remaining master is valid for readconnroute configured with 'router_options=slave'
 # https://jira.mariadb.org/browse/MXS-1889
add_test_executable(mxs1889.cpp mxs1889 mxs1889 LABELS REPL_BACKEND)

# MXS-421 Improved log facility
add_test_executable(mxs421_events.cpp mxs421_events mxs421_events LABELS REPL_BACKEND)

# MXS-1926: LOAD DATA LOCAL INFILE interrupted by server shutdown
# https://jira.mariadb.org/browse/MXS-1926
add_test_executable(mxs1926_killed_server.cpp mxs1926_killed_server mxs1926_killed_server LABELS readwritesplit REPL_BACKEND)

# MXS-1929: Runtime service creation
# https://jira.mariadb.org/browse/MXS-1929
add_test_executable(mxs1929_service_runtime.cpp mxs1929_service_runtime mxs1929_service_runtime LABELS REPL_BACKEND)

# MXS-1929: Runtime filter creation/destruction
# https://jira.mariadb.org/browse/MXS-1929
add_test_executable(mxs1929_filter_runtime.cpp mxs1929_filter_runtime mxs1929_filter_runtime LABELS REPL_BACKEND)

# MXS-1929: Create complete configuration at runtime
# https://jira.mariadb.org/browse/MXS-1929
add_test_executable(mxs1929_start_from_scratch.cpp mxs1929_start_from_scratch mxs1929_start_from_scratch LABELS REPL_BACKEND)

# MXS-1932: Hidden files are not ignored
# https://jira.mariadb.org/browse/MXS-1932
add_test_executable(mxs1932_hidden_cnf.cpp mxs1932_hidden_cnf replication LABELS REPL_BACKEND)

# MXS-1958: Users with insert-only privileges don't work
# https://jira.mariadb.org/browse/MXS-1958
add_test_executable(mxs1958_insert_priv.cpp mxs1958_insert_priv replication LABELS REPL_BACKEND)
# TODO: Remove this once MXS-1958 is fixed
set_tests_properties(mxs1958_insert_priv PROPERTIES WILL_FAIL TRUE)

# MXS-1849: Table family sharding router
# https://jira.mariadb.org/browse/MXS-1849
add_test_executable(mxs1849_table_sharding.cpp mxs1849_table_sharding mxs1849_table_sharding LABELS schemarouter BREAKS_REPL REPL_BACKEND)

# MXS-1985: MaxScale hangs on concurrent KILL processing
# https://jira.mariadb.org/browse/MXS-1985
add_test_executable(mxs1985_kill_hang.cpp mxs1985_kill_hang replication LABELS REPL_BACKEND)

# MXS-1113: Support of prepared statement for schemarouter
# https://jira.mariadb.org/browse/MXS-1113
add_test_executable(mxs1113_schemarouter_ps.cpp mxs1113_schemarouter_ps mxs1113_schemarouter_ps LABELS schemarouter BREAKS_REPL REPL_BACKEND)

# MXS-2037: Wildcards not working with source in Named Server Filter
# https://jira.mariadb.org/browse/MXS-2037
add_test_executable(mxs2037_namedserver_wildcards.cpp mxs2037_namedserver_wildcards mxs2037_namedserver_wildcards LABELS namedserverfilter LIGHT REPL_BACKEND)

# MXS-2043: Error "The MariaDB server is running with the --read-only option"
#           for "select for update"
# https://jira.mariadb.org/browse/MXS-2043
add_test_executable(mxs2043_select_for_update.cpp mxs2043_select_for_update replication LABELS REPL_BACKEND readwritesplit)

# MXS-2054: Hybrid clusters
add_test_executable(mxs2054_hybrid_cluster.cpp mxs2054_hybrid_cluster mxs2054_hybrid_cluster LABELS REPL_BACKEND)

# MXS-2111: mysql.user sometimes has SHA1 in authentication_string instead of password
add_test_executable(mxs2111_auth_string.cpp mxs2111_auth_string replication LABELS REPL_BACKEND)

# MXS-2115: Automatic version_string detection
add_test_executable(mxs2115_version_string.cpp mxs2115_version_string replication LABELS REPL_BACKEND)

# MXS-2273: Introduce server state BEING_DRAINED
add_test_executable(mxs2273_being_drained.cpp mxs2273_being_drained mxs2273_being_drained LABELS REPL_BACKEND)

# MXS-2295: COM_CHANGE_USER does not clear out session command history
add_test_executable(mxs2295_change_user_loop.cpp mxs2295_change_user_loop mxs2295_change_user_loop LABELS REPL_BACKEND readwritesplit)

# Debug assertion due to double-closed when a slave's response differs from the master
add_test_executable(crash_on_bad_sescmd.cpp crash_on_bad_sescmd crash_on_bad_sescmd LABELS readwritesplit REPL_BACKEND)

# MXS-2300: Prune session command history
add_test_executable(mxs2300_history_pruning.cpp mxs2300_history_pruning mxs2300_history_pruning LABELS readwritesplit REPL_BACKEND)

# MXS-2326: Routing hints aren't cloned in gwbuf_clone
add_test_executable(mxs2326_hint_clone.cpp mxs2326_hint_clone mxs2326_hint_clone LABELS readwritesplit REPL_BACKEND)

# MXS-2313: `rank` functional test
add_test_executable(mxs2313_rank.cpp mxs2313_rank mxs2313_rank LABELS readwritesplit REPL_BACKEND)

# MXS-2417: Ignore persisted configs with load_persisted_configs=false
add_test_executable(mxs2417_ignore_persisted_cnf.cpp mxs2417_ignore_persisted_cnf mxs2417_ignore_persisted_cnf LABELS REPL_BACKEND)

# MXS-2450: Crash on COM_CHANGE_USER with disable_sescmd_history=true
add_test_executable(mxs2450_change_user_crash.cpp mxs2450_change_user_crash mxs2450_change_user_crash LABELS REPL_BACKEND readwritesplit)

# MXS-1662: PAM admin authentication
add_test_executable(mxs1662_pam_admin.cpp mxs1662_pam_admin mxs1662_pam_admin LABELS REPL_BACKEND)

# MXS-2414: Block host after repeated authentication failures
add_test_executable(mxs2414_host_blocking.cpp mxs2414_host_blocking replication LABELS REPL_BACKEND)

# PAM authentication and various other authentication related issues:
# 1. PAM authentication, normal + roles (MXS-2478)
# 2. Multiple authenticators on the same listener (MXS-2497)
# 3. skip_authentication and match_host (MXS-2941)
# 4. lower_case_table_names
# 5. log_password_mismatch (MXS-2916)
add_test_executable(pam_authentication.cpp pam_authentication pam_authentication LABELS REPL_BACKEND)

# MXS-3225, MXS-2383 Pam two factor authentication (2FA) test. Works only on Centos so far, so label as
# HEAVY to prevent runs on general test runs.
add_test_executable(pam_authentication_2fa.cpp pam_authentication_2fa pam_authentication_2fa LABELS HEAVY REPL_BACKEND)

# Test three jdbc-connectors.
add_test_executable(jdbc_connectors.cc jdbc_connectors replication REPL_BACKEND)

# MXS-2350: On-demand connection creation
add_test_executable(mxs2350_lazy_connect.cpp mxs2350_lazy_connect mxs2350_lazy_connect LABELS REPL_BACKEND readwritesplit)

# MXS-2520: Allow master reconnection on reads
add_test_executable(mxs2520_master_read_reconnect.cpp mxs2520_master_read_reconnect mxs2520_master_read_reconnect LABELS REPL_BACKEND readwritesplit)

# MXS-2464: Crash in route_stored_query with ReadWriteSplit
add_test_executable(mxs2464_sescmd_reconnect.cpp mxs2464_sescmd_reconnect mxs2464_sescmd_reconnect LABELS REPL_BACKEND readwritesplit)

# MXS-2563: Failing debug assertion at rwsplitsession.cc:1129 : m_expected_responses == 0
add_test_executable(mxs2563_concurrent_slave_failure.cpp mxs2563_concurrent_slave_failure mxs2563_concurrent_slave_failure LABELS REPL_BACKEND readwritesplit)

# MXS-2521: COM_STMT_EXECUTE maybe return empty result
add_test_executable(mxs2521_double_exec.cpp mxs2521_double_exec mxs2521_double_exec LABELS REPL_BACKEND readwritesplit)

# MXS-2572: Add smartrouter tests.
add_test_executable(sr_basics.cpp sr_basics sr_basics LABELS REPL_BACKEND)
add_test_executable(smart_query.cpp smart_query smart_query LABELS REPL_BACKEND)

# MXS-2490: Direct execution doesn't work with MaxScale
add_test_executable(mxs2490_ps_execute_direct.cpp mxs2490_ps_execute_direct replication LABELS REPL_BACKEND readwritesplit)

# MXS-2609: Maxscale crash in RWSplitSession::retry_master_query()
add_test_executable(mxs2609_history_replay.cpp mxs2609_history_replay mxs2609_history_replay LABELS readwritesplit REPL_BACKEND)

# MXS-2621: Incorrect SQL if lower_case_table_names is used.
add_test_executable(mxs2621_lower_case_tables.cpp mxs2621_lower_case_tables mxs2621_lower_case_tables LABELS REPL_BACKEND)

# MXS-2631: Duplicate system tables not ignored
add_test_executable(mxs2631_ignore_system_tables.cpp mxs2631_ignore_system_tables mxs2631_ignore_system_tables LABELS schemarouter BREAKS_REPL REPL_BACKEND)

# Service-to-Service routing with readwritesplit
add_test_executable(sts_readwritesplit.cpp sts_readwritesplit sts_readwritesplit LABELS readwritesplit REPL_BACKEND)

# Service-to-Service routing with readconnroute
add_test_executable(sts_readconnroute.cpp sts_readconnroute sts_readconnroute LABELS readconnroute REPL_BACKEND)

# Service-to-Service routing with smartrouter
add_test_executable(sts_smartrouter.cpp sts_smartrouter sts_smartrouter LABELS smartrouter REPL_BACKEND)

# Service-to-Service routing runtime configuration
add_test_executable(sts_config.cpp sts_config sts_config LABELS REPL_BACKEND)

# Test that masking filter can handle multi-statements.
add_test_executable(mxs1719.cpp mxs1719 mxs1719 LABELS masking REPL_BACKEND)

# MXS-2057 systemd watchdog
add_test_executable(mxs2057_systemd_watchdog.cpp mxs2057_systemd_watchdog mxs2057_systemd_watchdog LABELS REPL_BACKEND)

# Tries prepared stmt 'SELECT 1,1,1,1...." with different nu,ber of '1'
add_test_executable(mxs314.cpp mxs314 replication LABELS readwritesplit LIGHT REPL_BACKEND)

# Basic mirror router test
add_test_executable(test_mirror.cpp test_mirror test_mirror LABELS mirror LIGHT REPL_BACKEND)

# Test connection_keepalive
add_test_executable(connection_keepalive.cpp connection_keepalive connection_keepalive LABELS readwritesplit REPL_BACKEND)

# MXS-2729 Thread rebalancing
add_test_executable(mxs2729_rebalance.cc mxs2729_rebalance replication LABELS REPL_BACKEND)

# MXS-2750: Test COM_STMT_EXECUTE metadata addition
add_test_executable(mxs2750_com_stmt_exec.cc mxs2750_com_stmt_exec replication LABELS readwritesplit REPL_BACKEND)

# MXS-2793: Schemarouter multi-table joins
add_test_executable(mxs2793_multi_table.cpp mxs2793_multi_table mxs2793_multi_table LABELS schemarouter REPL_BACKEND)

# MXS-2878: Verify that TLS is required
add_test_executable(mxs2878_monitor_ssl.cpp mxs2878_monitor_ssl mxs2878_monitor_ssl LABELS REPL_BACKEND)

# MXS-2939: Test that session commands trigger a reconnection
add_test_executable(mxs2939_sescmd_reconnect.cpp mxs2939_sescmd_reconnect mxs2939_sescmd_reconnect LABELS REPL_BACKEND readwritesplit)

# MXS-2145 and MXS-2623
add_test_executable(connection_init_queries.cc connection_init_queries connection_init_queries LABELS REPL_BACKEND)

# MXS-2919: Slaves with broken replication should not be used
add_test_executable(mxs2919_broken_slaves.cpp mxs2919_broken_slaves mxs2919_broken_slaves LABELS REPL_BACKEND readwritesplit)

# MXS-2972: Caching of shards must take servers into notice
add_test_executable(mxs2972_shard_caching.cc mxs2972_shard_caching mxs2972_shard_caching LABELS REPL_BACKEND schemarouter)

# MXS-3218: Crash with LOAD DATA LOCAL INFILE
add_test_executable(mxs3218_ldli_crash.cc mxs3218_ldli_crash replication LABELS REPL_BACKEND)

# MXS-3128: Listener SSL alteration test
add_test_executable(mxs3128_listener_ssl.cc mxs3128_listener_ssl replication LABELS REPL_BACKEND)

# MXS-3220: Crash on master reconnection with session command
add_test_executable(mxs3220_reconnect_crash.cc mxs3220_reconnect_crash mxs3220_reconnect_crash LABELS REPL_BACKEND readwritesplit)

# MXS-3184: FOUND_ROWS with prepared statements
add_test_executable(mxs3184_ps_found_rows.cc mxs3184_ps_found_rows replication LABELS REPL_BACKEND readwritesplit)

# MXS-3108: Alter session test
add_test_executable(mxs3108_alter_session.cc mxs3108_alter_session mxs3108_alter_session LABELS REPL_BACKEND)

############################################
# END: Normal tests                        #
############################################

############################################
# BEGIN: backend SSL tests                 #
############################################

add_test_derived(sql_queries_ssl sql_queries ssl LABELS readwritesplit REPL_BACKEND BACKEND_SSL)

# MXS-3128: Server SSL alteration test
add_test_executable(mxs3128_server_ssl.cc mxs3128_server_ssl replication LABELS REPL_BACKEND BACKEND_SSL)

############################################
# END: backend SSL  tests                  #
############################################

############################################
# BEGIN: avrorouter tests                  #
############################################

add_test_executable(avro.cpp avro avro LABELS avrorouter LIGHT BREAKS_REPL REPL_BACKEND)
add_test_executable(avro_alter.cpp avro_alter avro LABELS avrorouter LIGHT BREAKS_REPL REPL_BACKEND)

# Test of CDC protocol (avro listener)
add_test_executable(cdc_client.cpp cdc_client avro LABELS avrorouter BREAKS_REPL REPL_BACKEND)

# MXS-1542: Check that UTF16 strings work
# https://jira.mariadb.org/browse/MXS-1542
add_test_executable(mxs1542.cpp mxs1542 avro LABELS avrorouter REPL_BACKEND)

# MXS-1543: Avrorouter doesn't detect MIXED or STATEMENT format replication
# https://jira.mariadb.org/browse/MXS-1543
add_test_executable(mxs1543.cpp mxs1543 avro LABELS avrorouter REPL_BACKEND)

# MXS-1949: Warning for user load failure logged even when service has no users
# https://jira.mariadb.org/browse/MXS-1949
add_test_executable(mxs1949_warn_user_injection.cpp mxs1949_warn_user_injection avro LABELS REPL_BACKEND)

# MXS-2106: NULL values with avrorouter
add_test_executable(mxs2106_avro_null.cpp mxs2106_avro_null avro LABELS avrorouter REPL_BACKEND)

# MXS-1687: Avrorouter HA test (tests the same mechanism that kafkacdc uses)
add_test_executable(mxs1687_avro_ha.cc mxs1687_avro_ha mxs1687_avro_ha LABELS avrorouter REPL_BACKEND)

############################################
# END: avrorouter tests                    #
############################################

############################################
# BEGIN: tests with 15 machines backend    #
############################################

# temporarily added UNSTABLE HEAVY labels to prevent execution of these tests in daily rounds


# own long test
# 'long_test_time' variable defines time of execution (in seconds)
#add_test_executable_notest(long_test.cpp long_test_big replication LABELS readwritesplit REPL_BACKEND BIG_REPL_BACKEND UNSTABLE HEAVY)

# Tries INSERTs with size close to 0x0ffffff * N
add_test_derived(different_size_rwsplit_big different_size_rwsplit replication LABELS readwritesplit REPL_BACKEND BIG_REPL_BACKEND UNSTABLE HEAVY )

# Check how Maxscale works in case of one slave failure, only one slave is configured
add_test_derived(slave_failover_big slave_failover replication.one_slave LABELS readwritesplit REPL_BACKEND BIG_REPL_BACKEND UNSTABLE HEAVY)

# Execute queries of different size, check data is the same when accessing via Maxscale and directly to backend
add_test_derived(sql_queries_big sql_queries replication LABELS readwritesplit REPL_BACKEND BIG_REPL_BACKEND UNSTABLE HEAVY)

# Check temporal tables commands functionality
add_test_derived(temporal_tables_big temporal_tables replication LABELS readwritesplit REPL_BACKEND BIG_REPL_BACKEND UNSTABLE HEAVY)

# Check if prepared statement works via Maxscale (via RWSplit)
add_test_derived(prepared_statement_big prepared_statement replication LABELS readwritesplit REPL_BACKEND BIG_REPL_BACKEND UNSTABLE HEAVY)

# Connect to ReadConn in master mode and check if there is only one backend connection to master
add_test_derived(readconnrouter_master_big readconnrouter_master replication LABELS readconnroute REPL_BACKEND BIG_REPL_BACKEND UNSTABLE HEAVY)

# Creates 100 connections to ReadConn in slave mode and check if connections are distributed among all slaves
add_test_derived(readconnrouter_slave_big readconnrouter_slave replication LABELS readconnroute REPL_BACKEND BIG_REPL_BACKEND UNSTABLE HEAVY)

# Checks changes of COM_SELECT and COM_INSERT after queris to check if RWSplit sends queries to master or to slave depending on if it is write or read only query
add_test_derived(rw_select_insert_big rw_select_insert replication LABELS readwritesplit REPL_BACKEND BIG_REPL_BACKEND UNSTABLE HEAVY)

# Checks connections are distributed equaly among backends
add_test_derived(rwsplit_conn_num_big rwsplit_conn_num repl_lgc LABELS readwritesplit REPL_BACKEND BIG_REPL_BACKEND UNSTABLE HEAVY)

# Check that there is one connection to Master and one connection to one of slaves
add_test_derived(rwsplit_connect_big rwsplit_connect replication LABELS readwritesplit REPL_BACKEND BIG_REPL_BACKEND UNSTABLE HEAVY)

# Regression cases for the bug "Hint filter don't work if listed before regex filter in configuration file"
# (different filter sequence and configuration, but the same test, see .cnf for details)
add_test_derived(bug585_big bug587 bug585 LABELS regexfilter BIG_REPL_BACKEND REPL_BACKEND UNSTABLE HEAVY)
add_test_derived(bug587_big bug587 bug587 LABELS regexfilter hintfilter BIG_REPL_BACKEND REPL_BACKEND UNSTABLE HEAVY)
add_test_derived(bug587_1_big bug587 bug587_1 LABELS regexfilter hintfilter BIG_REPL_BACKEND REPL_BACKEND UNSTABLE HEAVY)
# Regression case for the bug "Routing Hints route to server sometimes doesn't work"
add_test_derived(bug471_big rhint_basic bug471_big LABELS readwritesplit hintfilter BIG_REPL_BACKEND REPL_BACKEND UNSTABLE HEAVY)

set_tests_properties(different_size_rwsplit_big PROPERTIES TIMEOUT 3600)
set_tests_properties(different_size_rwsplit PROPERTIES TIMEOUT 3600)
set_tests_properties(slave_failover_big PROPERTIES TIMEOUT 3600)
set_tests_properties(sql_queries_big PROPERTIES TIMEOUT 3600)
set_tests_properties(temporal_tables_big PROPERTIES TIMEOUT 3600)
set_tests_properties(prepared_statement_big PROPERTIES TIMEOUT 3600)
set_tests_properties(readconnrouter_master_big PROPERTIES TIMEOUT 3600)
set_tests_properties(readconnrouter_slave_big PROPERTIES TIMEOUT 3600)
set_tests_properties(rw_select_insert_big PROPERTIES TIMEOUT 3600)
set_tests_properties(rwsplit_conn_num_big PROPERTIES TIMEOUT 3600)
set_tests_properties(rwsplit_connect_big PROPERTIES TIMEOUT 3600)
set_tests_properties(bug585_big PROPERTIES TIMEOUT 3600)
set_tests_properties(bug587_big PROPERTIES TIMEOUT 3600)
set_tests_properties(bug587_1_big PROPERTIES TIMEOUT 3600)
set_tests_properties(bug471_big PROPERTIES TIMEOUT 3600)

############################################
# END: tests with 15 machines backend      #
############################################

############################################
# BEGIN: Galera tests                      #
############################################

# Crash in case of backend node in Galera cluster stopping and then reconnect to Maxscale
add_test_executable(bug676.cpp bug676 galera LABELS galeramon GALERA_BACKEND)

# Galera node priority test
add_test_executable(galera_priority.cpp galera_priority galera_priority LABELS galeramon LIGHT GALERA_BACKEND)

# INSERT extremelly big number of rows
add_test_executable(lots_of_rows.cpp lots_of_rows galera LABELS readwritesplit HEAVY GALERA_BACKEND)

# Prepearing and execution statements in the loop
add_test_executable(mxs244_prepared_stmt_loop.cpp mxs244_prepared_stmt_loop galera LABELS readwritesplit readconnroute LIGHT GALERA_BACKEND)

# Playing with blocking and unblocking nodes under INSERT load
add_test_executable(mxs564_big_dump.cpp mxs564_big_dump galera_mxs564 LABELS readwritesplit readconnroute GALERA_BACKEND)

# MXS-1476: priority value ignored when a Galera node rejoins with a lower wsrep_local_index than current master
# https://jira.mariadb.org/browse/MXS-1476
add_test_executable(mxs1476.cpp mxs1476 mxs1476 LABELS galeramon GALERA_BACKEND)

# MXS-1751: Maxscale crashes when certain config is in play (with nodes down)
# https://jira.mariadb.org/browse/MXS-1751
add_test_executable(mxs1751_available_when_donor_crash.cpp mxs1751_available_when_donor_crash mxs1751_available_when_donor_crash LABELS galeramon GALERA_BACKEND)

# Persistant connection test
add_test_executable(pers_01.cpp pers_01 pers_01 LABELS maxscale REPL_BACKEND readwritesplit GALERA_BACKEND)

# Test with extremely big blob inserting/selecting with > 16 mb data blocks
add_test_executable(mxs1110_16mb.cpp mxs1110_16mb longblob_filters LABELS readwritesplit readconnroute HEAVY REPL_BACKEND GALERA_BACKEND)

# A set of MariaDB server tests executed against Maxscale RWSplit (Galera backend)
add_test_script(mariadb_tests_hartmut_galera.sh mariadb_tests_hartmut.sh galera_hartmut LABELS readwritesplit GALERA_BACKEND)

# Crash with Galera and backend restart when persistant cfonnections are in use
add_test_derived(mxs361 pers_02 mxs361 mxs361 LABELS maxscale REPL_BACKEND GALERA_BACKEND)

# Simple connect test in bash, checks that defined in cmd line DB is selected (Galera backend)
add_test_script(mxs791_galera.sh mxs791.sh galera LABELS UNSTABLE HEAVY GALERA_BACKEND)

 # MXS-1585: Crash in MaxScale 2.1.12
 # https://jira.mariadb.org/browse/MXS-1585
add_test_executable(mxs1585.cpp mxs1585 mxs1585 LABELS GALERA_BACKEND)

# Test with persistant connections configured and big number iof opened connections ,expect no crash
add_test_executable(pers_02.cpp pers_02 pers_01 LABELS maxscale GALERA_BACKEND REPL_BACKEND readwritesplit)

# Test of external script execution
add_test_executable(script.cpp script script LABELS maxscale REPL_BACKEND GALERA_BACKEND)

# MXS-2441: Add support for read-only slaves to galeramon
add_test_executable(mxs2441_galera_slaves.cpp mxs2441_galera_slaves mxs2441_galera_slaves LABELS REPL_BACKEND GALERA_BACKEND)

############################################
# END: Galera tests                        #
############################################

############################################
# BEGIN: ColumnStore tests                 #
############################################

# MXS-2146: Add test case for csmon
# https://jira.mariadb.org/browse/MXS-2146
add_test_executable(csmon_test.cpp csmon_test csmon_test LABELS csmon CS_BACKEND)

############################################
# END: ColumnStore tests                   #
############################################

############################################
# BEGIN: tests for Xpand monitor        #
############################################

# creates Xpand cluster and connect Maxscale to it
add_test_executable(xpand_mon.cpp xpand_mon xpand_mon LABELS XPAND_BACKEND UNSTABLE)
set_tests_properties(xpand_mon PROPERTIES TIMEOUT 7200)

add_test_executable(xpand_basics.cpp xpand_basics xpand_basics LABELS XPAND_BACKEND)
set_tests_properties(xpand_basics PROPERTIES TIMEOUT 7200)

add_test_executable(xpand_transaction_replay.cpp xpand_transaction_replay xpand_transaction_replay LABELS XPAND_BACKEND)
set_tests_properties(xpand_transaction_replay PROPERTIES TIMEOUT 7200)

add_test_executable(xpand_distribution.cpp xpand_distribution xpand_distribution LABELS XPAND_BACKEND)

############################################
# END: tests for Xpand monitor          #
############################################

###############################
# DO NOT ADD TESTS AFTER THIS #
###############################

# The core testing library
add_linebreaks(TEST_DEFINITIONS TEST_DEFINITIONS)
configure_file(maxtest/src/test_info.cc.in maxtest/src/test_info.cc @ONLY)
add_subdirectory(maxtest)

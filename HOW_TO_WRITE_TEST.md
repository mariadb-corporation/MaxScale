# Creating a test case

## Test case basics

For every test case following should be created:
- test executable
- record in the 'templates' file 
- Maxscale configuration template (if test requires special Maxscale configuration)

## 'templates' file

'templates' file contains information about Maxscale configuration template for every test in plain text format:

\<test_executable_name\> \<suffix_of_cnf_template\>

Template itself should be:

cnf/maxscale.cnf.template.\<suffix_of_cnf_template\>

## Maxscale configuration template

All templates are in cnf/ directory:

cnf/maxscale.cnf.template.\<suffix_of_cnf_template\>

Template can contain following varables:

|Variable|Maeaning|
|--------|--------|
|###threads###| Number of Maxscale treads|
|###repl_server_IP_N###|IP of Master/Slave node N|
|###repl_server_port_N###|port of Master/Slave node N|
|###galera_server_IP_N###|IP of Galera node N|
|###galera_server_port_N###|port of Galera node N|

## Basics of the test

* initialize an object of TestConnections class
* set timeout before every operation which can got stuck, do not forget to disable timeout before long sleep()
* use TestConnections::tprintf function to print test log
* execute TestConnections::copy_all_logs at the end of test
* return TestConnections::global_result value
* do not leave any node blocked by firewall

## Class TestConnections

This class contains all information about Maxscale node and about all backend nodes as well as a set of functions
to handle Maxscale and backends, interact with Maxscale routers and Maxadmin.
Here is only list of main functions, for all details see Doxygen comments in [testconnections.h](testconnections.h)

Currently two backend sets are supported (represented by Mariadb_nodes class objects): 'repl' and 'galera' 
- contains all info and operations for Master/Slave and Galera setups
(see Doxygen comments in [mariadb_nodes.h](mariadb_nodes.h) )

It is assumed that following routers+listers are configured

|Router|Port|
|------|----|
|RWSplit|4006|
|ReadConn master|4008|
|ReadConn Slave|4009|
|binlog|5306|
|test case -specific|4016|


### Most important fuctions and variables

Please check Doxygen comments for details

#### TestConnections(int argc, char *argv[]);

* reads all information from environmental variables
* checks backends, if broken - does one attempt to restore
* create maxscale.cnf out of template and copy it to Maxscale node
* create needed directories, set access righs for them, cleanup logs, coredumps
* start Maxscale
* initialize internal structures 

#### Timeout functions

int set_timeout(int timeout_seconds)
stop_timeout()

If after set_timeout() a new call of set_timeout() or stop_timeout() is not done the test execution terminated, 
logs from Maxscale are copied to host.

#### Open connection functions

* functions which store MYSQL handler in TestConnections object (only one cnnection can be created by them, 
second call leads to MYSQL handler leak): 
 * int connect_maxscale();
 * int connect_rwsplit();
 * int connect_readconn_master();
 * int connect_maxscale_slave();
* fucntion which returns MYSQL handler (can be used to create a number of connections to each router)
 * MYSQL * open_rwsplit_connection()
 * MYSQL * open_readconn_master_connection()
 * MYSQL * open_readconn_slave_connection()
* int create_connections(int conn_N) - open and then close N connections to each router
 
#### Backend check and setup functions
* start_replication
* start_galera
* start_binlog
* check_repl
* check_galera

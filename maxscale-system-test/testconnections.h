#ifndef TESTCONNECTIONS_H
#define TESTCONNECTIONS_H

#include "mariadb_nodes.h"
#include "templates.h"
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>

/**
 * @brief Class contains references to Master/Slave and Galera test setups
 * Test setup should consist of two setups: one Master/Slave and one Galera.
 *
 * Maxscale should be configured separatelly for every test.
 *
 * Test setup should be described by enviromental variables:
 * - Maxscale_IP - IP adress of Maxscale machine
 * - Maxscale_User - User name to access Maxscale services
 * - Maxscale_Password - Password to access Maxscale services
 * - Maxscale_sshkey - ssh key for Maxscale machine
 * - maxscale_cnf - name of maxscale .cnf file (full)
 * - KillVMCommand - Command to kill a node (should handle one parameter: IP address of virtual machine to kill)
 * - StartVMCommand - Command to restart virtual machine (should handle one parameter: IP address of virtual machine to kill)
 * - GetLogsCommand - Command to copy log files from node virtual machines (should handle one parameter: IP address of virtual machine to kill)
 * - SysbenchDir - path to SysBench directory (sysbanch should be >= 0.5)
 * - node_N - Number of Master/Slave setup nodes
 * - node_NNN - IP address of node NNN (NNN - 3 digits node index starting from 000)
 * - node_port_NNN - MariaDB port for node NNN
 * - node_sshkey_NNN - ssh key to access node NNN (should be sutable for 'root' and 'ec2-user')
 * - node_User - User name to access Master/Slav setup
 * - node_Password - Password to access Master/Slave setup
 * - galera_N, galera_NNN, galera_port_NNN, galera_sshkey_NNN, galera_User, galera_Password - same for Galera setup
 *
 */
class TestConnections
{
private:
    /** Whether timeouts are enabled or not */
    bool enable_timeouts;
public:
    /**
     * @brief TestConnections constructor: reads environmental variables, copies MaxScale.cnf for MaxScale machine
     * @param test_exec_name Path to currect executable
     */
    TestConnections(int argc, char *argv[]);

    ~TestConnections();

    /**
     * @brief global_result Result of test, 0 if PASSED
     */
    int global_result;

    /**
     * @brief test_name Neme of the test
     */
    char * test_name;

    /**
     * @brief rwsplit_port RWSplit service port
     */
    int rwsplit_port;

    /**
     * @brief readconn_master_port ReadConnection in master mode service port
     */
    int readconn_master_port;

    /**
     * @brief readconn_slave_port ReadConnection in slave mode service port
     */
    int readconn_slave_port;

    /**
     * @brief binlog_port binlog router service port
     */
    int binlog_port;

    /**
     * @brief conn_rwsplit  MYSQL connection struct to RWSplit service
     */
    MYSQL *conn_rwsplit;

    /**
     * @brief conn_master   MYSQL connection struct to ReadConnection in master mode service
     */
    MYSQL *conn_master;

    /**
     * @brief conn_slave MYSQL connection struct to ReadConnection in slave mode service
     */
    MYSQL *conn_slave;

    /**
     * @brief routers Array of 3 MYSQL handlers which contains copies of conn_rwsplit, conn_master, conn_slave
     */
    MYSQL *routers[3];

    /**
     * @brief ports of 3 int which contains copies of rwsplit_port, readconn_master_port, readconn_slave_port
     */
    int ports[3];

    /**
     * @brief galera Mariadb_nodes object containing references to Galera setuo
     */
    Mariadb_nodes * galera;

    /**
     * @brief repl Mariadb_nodes object containing references to Master/Slave setuo
     */
    Mariadb_nodes * repl;

    /**
     * @brief Get MaxScale IP address
     *
     * @return The current IP address of MaxScale
     */
    char* maxscale_ip() const;

    /**
     * @brief Maxscale_IP   Maxscale machine IP address
     */
    char maxscale_IP[1024];

    /**
     * @brief Maxscale_IP6   Maxscale machine IP address (IPv6)
     */
    char maxscale_IP6[1024];

    /**
     * @brief use_ipv6 If true IPv6 addresses will be used to connect Maxscale and backed
     * Also IPv6 addresses go to maxscale.cnf
     */
    bool use_ipv6;

    /**
     * @brief maxscale_hostname  Maxscale machine 'hostname' value
     */
    char maxscale_hostname[1024];

    /**
     * @brief Maxscale_User User name to access Maxscale services
     */
    char maxscale_user[256];

    /**
     * @brief Maxscale_Password Password to access Maxscale services
     */
    char maxscale_password[256];

    /**
     * @brief maxadmin_Password Password to access Maxadmin tool
     */
    char maxadmin_password[256];

    /**
     * @brief Maxscale_sshkey   ssh key for Maxscale machine
     */
    char maxscale_keyfile[4096];

    /**
     * @brief GetLogsCommand Command to copy log files from node virtual machines (should handle one parameter: IP address of virtual machine to kill)
     */
    char get_logs_command[4096];

    /**
     * @brief make_snapshot_command Command line to create a snapshot of all VMs
     */
    char take_snapshot_command[4096];

    /**
     * @brief revert_snapshot_command Command line to revert a snapshot of all VMs
     */
    char revert_snapshot_command[4096];

    /**
     * @brief use_snapshots if TRUE every test is trying to revert snapshot before running the test
     */
    bool use_snapshots;

    /**
     * @brief SysbenchDir   path to SysBench directory (sysbanch should be >= 0.5)
     */
    char sysbench_dir[4096];

    /**
      * @brief maxscale_cnf full name of Maxscale configuration file
      */
    char maxscale_cnf[4096];

    /**
      * @brief maxscale_log_dir name of log files directory
      */
    char maxscale_log_dir[4096];

    /**
      * @brief maxscale_lbinog_dir name of binlog files (for binlog router) directory
      */
    char maxscale_binlog_dir[4096];

    /**
     * @brief maxscale_access_user username to access test machines
     */
    char maxscale_access_user[256];

    /**
     * @brief maxscale_access_homedir home directory of access_user
     */
    char maxscale_access_homedir[256];

    /**
     * @brief maxscale_access_sudo empty if sudo is not needed or "sudo " if sudo is needed.
     */
    char maxscale_access_sudo[64];

    /**
     * @brief copy_mariadb_logs copies MariaDB logs from backend
     * @param repl Mariadb_nodes object
     * @param prefix file name prefix
     * @return 0 if success
     */
    int copy_mariadb_logs(Mariadb_nodes *repl, char * prefix);

    /**
     * @brief no_backend_log_copy if true logs from backends are not copied (needed if case of Aurora RDS backend or similar)
     */
    bool no_backend_log_copy;

    /**
     * @brief verbose if true more printing activated
     */
    bool verbose;

    /**
     * @brief smoke if true all tests are executed in quick mode
     */
    bool smoke;

    /**
     * @brief binlog_cmd_option index of mariadb start option
     */
    int binlog_cmd_option;

    /**
     * @brief ssl if true ssl will be used
     */
    int ssl;

    /**
     * @brief backend_ssl if true ssl configuratio for all servers will be added
     */
    bool backend_ssl;

    /**
     * @brief no_galera Do not check, restart and use Galera setup; all Galera tests will fail
     */
    bool no_galera;

    /**
    * @brief ssl_options string with ssl configuration for command line client
    */
    char ssl_options[1024];

    /**
     * @brief threads Number of Maxscale threads
     */
    int threads;

    /**
     * @brief timeout seconds until test termination
     */
    long int timeout;

    /**
     * @brief log_copy_interval seconds between log copying
     */
    long int log_copy_interval;

    /**
     * @brief log_copy_interval seconds until next log copying
     */
    long int log_copy_to_go;

    /**
     * @brief timeout_thread_p pointer to timeout thread
     */
    pthread_t timeout_thread_p;

    /**
     * @brief log_copy_thread_p pointer to log copying thread
     */
    pthread_t log_copy_thread_p;

    /**
     * @brief start_time time when test was started (used by printf to print Timestamp)
     */
    timeval start_time;

    /** Check whether all nodes are in a valid state */
    static void check_nodes(bool value);

    /** Skip initial start of MaxScale */
    static void skip_maxscale_start(bool value);

    /** Test requires a certain backend version  */
    static void require_repl_version(const char *version);
    static void require_galera_version(const char *version);

    /**
     * @brief add_result adds result to global_result and prints error message if result is not 0
     * @param result 0 if step PASSED
     * @param format ... message to pring if result is not 0
     */
    void add_result(int result, const char *format, ...);

    /**
     * @brief ReadEnv Reads all Maxscale and Master/Slave and Galera setups info from environmental variables
     * @return 0 in case of success
     */
    int read_env();

    /**
     * @brief PrintIP   Prints all Maxscale and Master/Slave and Galera setups info
     * @return 0
     */
    int print_env();

    /**
     * @brief InitMaxscale  Copies MaxSclae.cnf and start MaxScale
     * @return 0 if case of success
     */
    int init_maxscale();

    /**
     * @brief ConnectMaxscale   Opens connections to RWSplit, ReadConn master and ReadConn slave Maxscale services
     * Opens connections to RWSplit, ReadConn master and ReadConn slave Maxscale services
     * Connections stored in conn_rwsplit, conn_master and conn_slave MYSQL structs
     * @return 0 in case of success
     */
    int connect_maxscale();

    /**
     * @brief CloseMaxscaleConn Closes connection that were opened by ConnectMaxscale()
     * @return 0
     */
    int close_maxscale_connections();

    /**
     * @brief ConnectRWSplit    Opens connections to RWSplit and store MYSQL struct in conn_rwsplit
     * @return 0 in case of success
     */
    int connect_rwsplit();

    /**
     * @brief ConnectReadMaster Opens connections to ReadConn master and store MYSQL struct in conn_master
     * @return 0 in case of success
     */
    int connect_readconn_master();

    /**
     * @brief ConnectReadSlave Opens connections to ReadConn slave and store MYSQL struct in conn_slave
     * @return 0 in case of success
     */
    int connect_readconn_slave();

    /**
     * @brief OpenRWSplitConn   Opens new connections to RWSplit and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return MYSQL struct
     */
    MYSQL * open_rwsplit_connection()
    {
        return open_conn(rwsplit_port, maxscale_IP, maxscale_user, maxscale_password, ssl);
    }

    /**
     * @brief OpenReadMasterConn    Opens new connections to ReadConn master and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return MYSQL struct
     */
    MYSQL * open_readconn_master_connection()
    {
        return open_conn(readconn_master_port, maxscale_IP, maxscale_user, maxscale_password, ssl);
    }

    /**
     * @brief OpenReadSlaveConn    Opens new connections to ReadConn slave and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return  MYSQL struct
     */
    MYSQL * open_readconn_slave_connection()
    {
        return open_conn(readconn_slave_port, maxscale_IP, maxscale_user, maxscale_password, ssl);
    }

    /**
     * @brief CloseRWSplit Closes RWplit connections stored in conn_rwsplit
     */
    void close_rwsplit()
    {
        mysql_close(conn_rwsplit);
        conn_rwsplit = NULL;
    }

    /**
     * @brief CloseReadMaster Closes ReadConn master connections stored in conn_master
     */
    void close_readconn_master()
    {
        mysql_close(conn_master);
        conn_master = NULL;
    }

    /**
     * @brief CloseReadSlave Closes ReadConn slave connections stored in conn_slave
     */
    void close_readconn_slave()
    {
        mysql_close(conn_slave);
        conn_slave = NULL;
    }

    /**
     * @brief restart_maxscale Issues 'service maxscale restart' command
     */
    int restart_maxscale();

    /**
     * @brief start_maxscale Issues 'service maxscale start' command
     */
    int start_maxscale();

    /**
     * @brief stop_maxscale Issues 'service maxscale stop' command
     */
    int stop_maxscale();

    /**
     * @brief start_binlog configure first node as Master, Second as slave connected to Master and others as slave connected to MaxScale binlog router
     * @return  0 in case of success
     */
    int start_binlog();

    /**
     * @brief prepare_binlog clean up binlog directory, set proper access rights to it
     * @return 0
     */
    int prepare_binlog();

    /**
     * @brief start_mm configure first node as Master for second, Second as Master for first
     * @return  0 in case of success
     */
    int start_mm();

    /**
     * @brief copy_all_logs Copies all MaxScale logs and (if happens) core to current workspace
     */
    int copy_all_logs();

    /**
     * @brief copy_all_logs_periodic Copies all MaxScale logs and (if happens) core to current workspace and sends time stemp to log copying script
     */
    int copy_all_logs_periodic();

    /**
     * @brief Generate command line to execute command on the Maxscale ode via ssh
     * @param cmd result
     * @param ssh command to execute
     * @param sudo if true the command is executed with root privelegues
     */
    void generate_ssh_cmd(char * cmd, char * ssh, bool sudo);

    /**
     * @brief Execute a command via ssh on the MaxScale machine
     * @param ssh ssh command to execute on the MaxScale machine
     * @return Output of the command or NULL if the command failed to execute
     */
    char* ssh_maxscale_output(bool sudo, const char* format, ...);

    /**
     * @brief Execute a shell command on Maxscale
     * @param sudo Use root
     * @param format printf style format string
     * @return 0 on success
     */
    int ssh_maxscale(bool sudo, const char* format, ...);

    /**
     * @brief Copy a local file to the MaxScale machine
     * @param src Source file on the local filesystem
     * @param dest Destination file on the MaxScale machine's file system
     * @return exit code of the system command
     */
    int copy_to_maxscale(const char* src, const char* dest);

    /**
     * @brief Copy a remote file from the MaxScale machine
     * @param src Source file on the remote filesystem
     * @param dest Destination file on the local file system
     * @return exit code of the system command
     */
    int copy_from_maxscale(char* src, char* dest);

    /**
     * @brief Test that connections to MaxScale are in the expected state
     * @param rw_split State of the MaxScale connection to Readwritesplit. True for working connection, false for no connection.
     * @param rc_master State of the MaxScale connection to Readconnroute Master. True for working connection, false for no connection.
     * @param rc_slave State of the MaxScale connection to Readconnroute Slave. True for working connection, false for no connection.
     * @return  0 if connections are in the expected state
     */
    int test_maxscale_connections(bool rw_split,
                                  bool rc_master,
                                  bool rc_slave);

    /**
     * @brief Create a number of connections to all services, run simple query, close all connections
     * @param conn_N number of connections
     * @param rwsplit_flag if true connections to RWSplit router will be created, if false - no connections to RWSplit
     * @param master_flag if true connections to ReadConn master router will be created, if false - no connections to ReadConn master
     * @param slave_flag if true connections to ReadConn slave router will be created, if false - no connections to ReadConn slave
     * @param galera_flag if true connections to RWSplit router with Galera backend will be created, if false - no connections to RWSplit with Galera backend
     * @return  0 in case of success
     */
    int create_connections(int conn_N, bool rwsplit_flag, bool master_flag, bool slave_flag, bool galera_flag);

    /**
     * Trying to get client IP address by connection to DB via RWSplit and execution 'show processlist'
     *
     * @param ip client IP address as it visible by Maxscale
     * @return 0 in case of success
     */
    int get_client_ip(char * ip);

    /**
     * @brief set_timeout startes timeout thread which terminates test application after timeout_seconds
     * @param timeout_seconds timeout time
     * @return 0 if success
     */
    int set_timeout(long int timeout_seconds);

    /**
     * @brief set_log_copy_interval sets interval for periodic log copying
     * @param interval_seconds interval in seconds
     * @return 0 if success
     */
    int set_log_copy_interval(long int interval_seconds);

    /**
     * @brief stop_timeout stops timeout thread
     * @return 0
     */
    int stop_timeout();

    /**
     * @brief printf adds timestam to printf
     * @param __format
     * @return
     */
    int tprintf(const char *format, ...);

    /**
     * @brief Creats t1 table, insert data into it and checks if data can be correctly read from all Maxscale services
     * @param Test Pointer to TestConnections object that contains references to test setup
     * @param N number of INSERTs; every next INSERT is longer 16 times in compare with previous one: for N=4 last INSERT is about 700kb long
     * @return 0 in case of no error and all checks are ok
     */
    int insert_select(int N);

    /**
     * @brief Executes USE command for all Maxscale service and all Master/Slave backend nodes
     * @param Test Pointer to TestConnections object that contains references to test setup
     * @param db Name of DB in 'USE' command
     * @return 0 in case of success
     */
    int use_db(char * db);

    /**
     * @brief Checks if table t1 exists in DB
     * @param presence expected result
     * @param db DB name
     * @return 0 if (t1 table exists AND presence=TRUE) OR (t1 table does not exist AND presence=false)
     */

    int check_t1_table(bool presence, char * db);

    /**
     * @brief CheckLogErr Reads error log and tried to search for given string
     * @param err_msg Error message to search in the log
     * @param expected TRUE if err_msg is expedted in the log, false if err_msg should NOT be in the log
     * @return 0 if (err_msg is found AND expected is TRUE) OR (err_msg is NOT found in the log AND expected is false)
     */
    void check_log_err(const char * err_msg, bool expected);

    /**
     * @brief FindConnectedSlave Finds slave node which has connections from MaxScale
     * @param Test TestConnections object which contains info about test setup
     * @param global_result pointer to variable which is increased in case of error
     * @return index of found slave node
     */
    int find_connected_slave(int * global_result);

    /**
     * @brief FindConnectedSlave1 same as FindConnectedSlave() but does not increase global_result
     * @param Test  TestConnections object which contains info about test setup
     * @return index of found slave node
     */
    int find_connected_slave1();

    /**
     * @brief CheckMaxscaleAlive Checks if MaxScale is alive
     * Reads test setup info from enviromental variables and tries to connect to all Maxscale services to check if i is alive.
     * Also 'show processlist' query is executed using all services
     * @return 0 in case if success
     */
    int check_maxscale_alive();

    /**
     * @brief try_query Executes SQL query and repors error
     * @param conn MYSQL struct
     * @param sql SQL string
     * @return 0 if ok
     */
    int try_query(MYSQL *conn, const char *sql, ...);

    /**
     * @brief try_query_all Executes SQL query on all MaxScale connections
     * @param sql SQL string
     * @return 0 if ok
     */
    int try_query_all(const char *sql);

    /**
     * @brief find_master_maxadmin Tries to find node with 'Master' status using Maxadmin connand 'show server'
     * @param nodes Mariadb_nodes object
     * @return node index if one master found, -1 if no master found or several masters found
     */
    int find_master_maxadmin(Mariadb_nodes * nodes);
    int find_slave_maxadmin(Mariadb_nodes * nodes);

    int execute_maxadmin_command(char * cmd);
    int execute_maxadmin_command_print(char * cmd);
    int check_maxadmin_param(const char *command, const  char *param, const  char *value);
    int get_maxadmin_param(char *command, char *param, char *result);
    void check_current_operations(int value);
    void check_current_connections(int value);

    /**
     * @brief check_maxscale_processes Check if number of running Maxscale processes is equal to 'expected'
     * @param expected expected number of Maxscale processes
     * @return 0 if check is done
     */
    int check_maxscale_processes(int expected);

    /**
     * @brief list_dirs Execute 'ls' on binlog directory on all repl nodes and on Maxscale node
     * @return 0
     */
    int list_dirs();

    /**
     * @brief get_maxscale_memsize Gets size of the memory consumed by Maxscale process
     * @return memory size in kilobytes
     */
    long unsigned get_maxscale_memsize();

    /**
     * @brief make_snapshot Makes a snapshot for all running VMs
     * @param snapshot_name name of created snapshot
     * @return 0 in case of success or mdbci error code in case of error
     */
    int take_snapshot(char * snapshot_name);

    /**
     * @brief revert_snapshot Revert snapshot for all running VMs
     * @param snapshot_name name of snapshot to revert
     * @return 0 in case of success or mdbci error code in case of error
     */
    int revert_snapshot(char * snapshot_name);

    /**
     * @brief Test a bad configuration
     * @param config Name of the config template
     * @return Always false, the test will time out if the loading is successful
     */
    bool test_bad_config(const char *config);

    /**
     * @brief Process a template configuration file
     *
     * @param dest Destination file name for actual configuration file
     */
    void process_template(const char *src, const char *dest = "/etc/maxscale.cnf");
};

/**
 * @brief timeout_thread Thread which terminates test application after 'timeout' milliseconds
 * @param ptr pointer to TestConnections object
 * @return void
 */
void * timeout_thread(void *ptr );

/**
 * @brief log_copy_thread Thread which peridically copies logs from Maxscale machine
 * @param ptr pointer to TestConnections object
 * @return void
 */
void * log_copy_thread(void *ptr );

#endif // TESTCONNECTIONS_H

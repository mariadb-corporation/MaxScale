#ifndef MARIADB_NODES_H
#define MARIADB_NODES_H

/**
 * @file mariadb_nodes.h - backend nodes routines
 *
 * @verbatim
 * Revision History
 *
 * Date     Who     Description
 * 17/11/14 Timofey Turenko Initial implementation
 *
 * @endverbatim
 */


#include "mariadb_func.h"
#include <errno.h>
#include <string>
#include "nodes.h"

/**
 * @brief A class to handle backend nodes
 * Contains references up to 256 nodes, info about IP, port, ssh key, use name and password for each node
 * Node parameters should be defined in the enviromental variables in the follwing way:
 * prefix_N - N number of nodes in the setup
 * prefix_NNN - IP adress of the node (NNN 3 digits node index)
 * prefix_port_NNN - MariaDB port number of the node
 * prefix_User - User name to access backend setup (should have full access to 'test' DB with GRANT OPTION)
 * prefix_Password - Password to access backend setup
 */
class Mariadb_nodes : public Nodes
{
public:
    /**
     * @brief Constructor
     * @param pref  name of backend setup (like 'repl' or 'galera')
     */
    Mariadb_nodes(const char* pref, const char* test_cwd, bool verbose);

    virtual ~Mariadb_nodes();

    /**
     * @brief  MYSQL structs for every backend node
     */
    MYSQL* nodes[256];
    /**
     * @brief  IP address strings for every backend node
     */

    /**
     * @brief  MariaDB port for every backend node
     */
    int port[256];
    /**
     * @brief Unix socket to connecto to MariaDB
     */
    char socket[256][1024];
    /**
     * @brief 'socket=$socket' line
     */
    char socket_cmd[256][1024];

    /**
     * @brief   User name to access backend nodes
     */
    char user_name[256];
    /**
     * @brief   Password to access backend nodes
     */
    char password[256];
    /**
     * @brief master index of node which was last configured to be Master
     */
    int master;

    /**
     * @brief start_db_command Command to start DB server
     */
    char start_db_command[256][4096];

    /**
     * @brief stop_db_command Command to start DB server
     */
    char stop_db_command[256][4096];

    /**
     * @brief cleanup_db_command Command to remove all
     * data files and re-install DB with mysql_install_db
     */
    char cleanup_db_command[256][4096];

    /**
     * @brief ssl if true ssl will be used
     */
    int ssl;

    /**
     * @brief no_set_pos if set to true setup_binlog function do not set log position
     */
    bool no_set_pos;

    /**
     * @brief version Value of @@version
     */
    char version[256][256];

    /**
     * @brief version major part of number value of @@version
     */
    char version_major[256][256];

    /**
     * @brief version Number part of @@version
     */
    char version_number[256][256];

    /**
     * @brief connect open connections to all nodes
     * @return 0  in case of success
     */

    /**
     * @brief v51 true indicates that one backed is 5.1
     */
    bool v51;

    /**
     * @brief test_dir path to test application
     */
    char test_dir[4096];

    /**
     * @brief List of blocked nodes
     */
    bool blocked[256];

    /**
     * @brief  Open connctions to all backend nodes (to 'test' DB)
     * @return 0 in case of success
     */

    /**
     * @brief make_snapshot_command Command line to create a snapshot of all VMs
     */
    char* take_snapshot_command;

    /**
     * @brief revert_snapshot_command Command line to revert a snapshot of all VMs
     */
    char* revert_snapshot_command;

    int connect(int i, const std::string& db = "test");
    int connect(const std::string& db = "test");

    /**
     * Get a Connection to a node
     */
    Connection get_connection(int i, const std::string& db = "test")
    {
        return Connection(IP[i], port[i], user_name, password, db, ssl);
    }

    /**
     * Repeatedly try to connect with one second sleep in between attempts
     *
     * @return True on success
     */
    bool robust_connect(int n);

    /**
     * @brief Close connections opened by connect()
     *
     * This sets the values of used @c nodes to NULL.
     */
    void close_connections();

    // Alias for close_connections()
    void disconnect()
    {
        close_connections();
    }

    /**
     * @brief reads IP, Ports, sshkeys for every node from enviromental variables as well as number of nodes
     *(N) and  User/Password
     */
    void read_env();
    /**
     * @brief  prints all nodes information
     * @return 0
     */
    void print_env();

    /**
     * @brief find_master Tries to find Master node
     * @return Index of Master node
     */
    int find_master();
    /**
     * @brief change_master set a new master node for Master/Slave setup
     * @param NewMaster index of new Master node
     * @param OldMaster index of current Master node
     */
    void change_master(int NewMaster, int OldMaster);

    /**
     * @brief stop_nodes stops mysqld on all nodes
     * @return  0 in case of success
     */
    int stop_nodes();

    /**
     * @brief stop_slaves isues 'stop slave;' to all nodes
     * @return  0 in case of success
     */
    int stop_slaves();

    /**
     * @brief cleanup_db_node Removes all data files and reinstall DB
     * with mysql_install_db
     * @param node
     * @return 0 in case of success
     */
    int cleanup_db_node(int node);

    /**
     * @brief cleanup_db_node Removes all data files and reinstall DB
     * with mysql_install_db for all nodes
     * @param node
     * @return 0 in case of success
     */
    int cleanup_db_nodes();

    /**
     * @brief configures nodes and starts Master/Slave replication
     * @return  0 in case of success
     */
    virtual int start_replication();

    // Create the default users used by all tests
    void create_users(int node);

    /**
     * @brif BlockNode setup firewall on a backend node to block MariaDB port
     * @param node Index of node to block
     * @return 0 in case of success
     */
    int block_node(int node);

    /**
     * @brief UnblockNode setup firewall on a backend node to unblock MariaDB port
     * @param node Index of node to unblock
     * @return 0 in case of success
     */
    int unblock_node(int node);


    /**
     * @brief Unblock all nodes for this cluster
     * @return 0 in case of success
     */
    int unblock_all_nodes();

    /**
     * @brief clean_iptables removes all itables rules connected to MariaDB port to avoid duplicates
     * @param node Index of node to clean
     * @return 0 in case of success
     */
    int clean_iptables(int node);

    /**
     * @brief Stop DB server on the node
     * @param node Node index
     * @return 0 if success
     */
    int stop_node(int node);

    /**
     * @brief Start DB server on the node
     * @param node Node index
     * @param param command line parameters for DB server start command
     * @return 0 if success
     */
    int start_node(int node, const char* param = "");

    /**
     * @brief Check if all slaves have "Slave_IO_Running" set to "Yes" and master has N-1 slaves
     * @param master Index of master node
     * @return 0 if everything is ok
     */
    virtual int check_replication();

    /**
     * @brief executes 'CHANGE MASTER TO ..' and 'START SLAVE'
     * @param MYSQL conn struct of slave node
     * @param master_host IP address of master node
     * @param master_port port of master node
     * @param log_file name of log file
     * @param log_pos initial position
     * @return 0 if everything is ok
     */
    int set_slave(MYSQL* conn, char master_host[], int master_port, char log_file[], char log_pos[]);

    /**
     * @brief Creates 'repl' user on all nodes
     * @return 0 if everything is ok
     */
    int set_repl_user();

    /**
     * @brief Get the server_id of the node
     * @param index The index of the node whose server_id to retrieve
     * @return Node id of the server or -1 on error
     */
    int         get_server_id(int index);
    std::string get_server_id_str(int index);

    /**
     * Get server IDs of all servers
     *
     * @return List of server IDs
     */
    std::vector<int> get_all_server_ids();

    /**
     * @brief Execute 'mysqladmin flush-hosts' on all nodes
     * @return 0 in case of success
     */
    int flush_hosts();

    /**
     * @brief Execute query on all nodes
     * @param sql query to execute
     * @return 0 in case of success
     */
    int execute_query_all_nodes(const char* sql);

    /**
     * @brief execute 'SELECT @@version' against one node and store result in 'version' field
     * @param i Node index
     * @return 0 in case of success
     */
    int get_version(int i);

    /**
     * @brief execute 'SELECT @@version' against all nodes and store result in 'version' field
     * @return 0 in case of success
     */
    int get_versions();

    /**
     * @brief Return lowest server version in the cluster
     * @return The version string of the server with the lowest version number
     */
    std::string get_lowest_version();

    /**
     * @brief truncate_mariadb_logs clean ups MariaDB logs on backend nodes
     * @return 0 if success
     */
    int truncate_mariadb_logs();

    /**
     * @brief configure_ssl Modifies my.cnf in order to enable ssl, redefine access user to require ssl
     * @return 0 if success
     */
    int configure_ssl(bool require);

    /**
     * @brief disable_ssl Modifies my.cnf in order to get rid of ssl, redefine access user to allow
     * connections without ssl
     * @return 0 if success
     */
    int disable_ssl();

    /**
     * @brief Synchronize slaves with the master
     *
     * Only works with master-slave replication and should not be used with Galera clusters.
     * The function expects that the first node, @c nodes[0], is the master.
     */
    void sync_slaves(int node = 0);

    /**
     * @brief Close all connections to this node
     *
     * This will kill all connections that have been created to this node.
     */
    void close_active_connections();

    /**
     * @brief Check and fix replication
     */
    bool fix_replication();

    /**
     * Copy current server settings to a backup directory. Any old backups are overwritten.
     *
     * @param node Node to modify
     */
    void stash_server_settings(int node);

    /**
     * Restore server settings from a backup directory. Current settings files are overwritten and
     * backup settings files are removed.
     *
     * @param node Node to modify
     */
    void restore_server_settings(int node);

    /**
     * Comment any line starting with the given setting name in server settings files.
     *
     * @param node Node to modify
     * @param setting Setting to remove
     */
    void disable_server_setting(int node, const char* setting);

    /**
     * Add the following lines to the /etc/mysql.cnf.d/server.cnf-file:
     * [server]
     * parameter
     *
     * @param node Node to modify
     * @param setting Line to add
     */
    void add_server_setting(int node, const char* setting);

    /**
     * Get the configuration file name for a particular node
     *
     * @param node Node number for which the configuration is requested
     *
     * @return The name of the configuration file
     */
    virtual std::string get_config_name(int node);

    /**
     * Restore the original configuration for all servers
     */
    void reset_server_settings();
    // Same but for an individual server
    void reset_server_settings(int node);

    /**
     * @brief revert_nodes_snapshot Execute MDBCI snapshot revert command for all nodes
     * @return true in case of success
     */
    bool revert_nodes_snapshot();

    /**
     * @brief prepare_server Initialize MariaDB setup (run mysql_install_db) and create test users
     * Tries to detect Mysql 5.7 installation and disable 'validate_password' pluging
     * @param i Node index
     * @return 0 in case of success
     */
    virtual int prepare_server(int i);
    int         prepare_servers();

    /**
     * Static functions
     */

    /** Whether to require GTID based replication, defaults to false */
    static void require_gtid(bool value);

    /**
     * Configure a server as a slave of another server
     *
     * The servers are configured with GTID replicating using the configured
     * GTID position, either slave_pos or current_pos.
     *
     * @param slave  The node index to assign as slave
     * @param master The node index of the master
     * @param type   Replication type
     */
    void replicate_from(int slave, int master, const char* type = "current_pos");

private:

    bool check_master_node(MYSQL* conn);
    bool bad_slave_thread_status(MYSQL* conn, const char* field, int node);
};

class Galera_nodes : public Mariadb_nodes
{
public:

    Galera_nodes(const char* pref, const char* test_cwd, bool verbose)
        : Mariadb_nodes(pref, test_cwd, verbose)
    {
    }

    int start_galera();

    virtual int start_replication()
    {
        return start_galera();
    }

    int check_galera();

    virtual int check_replication()
    {
        return check_galera();
    }

    std::string get_config_name(int node) override;
};

#endif      // MARIADB_NODES_H

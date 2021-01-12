#pragma once

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

#include <cerrno>
#include <string>
#include <maxtest/mariadb_func.hh>
#include <maxtest/nodes.hh>

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
    enum class Type
    {
        MARIADB,
        GALERA,
        COLUMNSTORE,
        XPAND
    };

    /**
     * @brief Constructor
     * @param pref  name of backend setup (like 'repl' or 'galera')
     */
    Mariadb_nodes(const char* pref, SharedData& shared, const std::string& network_config,
                  Type type);

    Mariadb_nodes(SharedData& shared, const std::string& network_config);

    bool setup() override;

    virtual ~Mariadb_nodes();

    void set_use_ipv6(bool use_ipv6);

    const char* ip(int i = 0) const;
    const char* ip4(int i = 0) const;
    const char* ip6(int i = 0) const;
    const char* ip_private(int i = 0) const;
    const char* access_homedir(int i = 0) const;
    const char* access_sudo(int i = 0) const;

    const std::string& prefix() const;

    Type type() const
    {
        return m_type;
    }

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
    std::string socket[256];
    /**
     * @brief 'socket=$socket' line
     */
    std::string socket_cmd[256];

    /**
     * @brief   User name to access backend nodes
     */
    std::string user_name;
    /**
     * @brief   Password to access backend nodes
     */
    std::string password;
    /**
     * @brief master index of node which was last configured to be Master
     */
    int master;

    /**
     * @brief start_db_command Command to start DB server
     */
    std::string start_db_command[256];

    /**
     * @brief stop_db_command Command to start DB server
     */
    std::string stop_db_command[256];

    /**
     * @brief cleanup_db_command Command to remove all
     * data files and re-install DB with mysql_install_db
     */
    std::string cleanup_db_command[256];

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
    const char* take_snapshot_command;

    /**
     * @brief revert_snapshot_command Command line to revert a snapshot of all VMs
     */
    const char* revert_snapshot_command;

    int connect(int i, const std::string& db = "test");
    int connect(const std::string& db = "test");

    /**
     * Get a Connection to a node
     */
    Connection get_connection(int i, const std::string& db = "test")
    {
        return Connection(ip4(i), port[i], user_name, password, db, ssl);
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
     * Create the default users used by all tests on all nodes.
     *
     * @return 0 in case of success.
     */
    int create_users();

    /**
     * Blocks `src` from communicating with `dest`
     */
    void block_node_from_node(int src, int dest);

    /**
     * Unblocks the block added by block_node_from_node
     */
    void unblock_node_from_node(int src, int dest);

    /**
     * @param node Index of node to block.
     * @return The command used for blocking a node.
     */
    virtual std::string block_command(int node) const;

    /**
     * @param node Index of node to unblock.
     * @return The command used for unblocking a node.
     */
    virtual std::string unblock_command(int node) const;

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
     * @brief Block all nodes for this cluster
     * @return 0 in case of success
     */
    int block_all_nodes();

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
    int set_slave(MYSQL* conn, const char* master_host, int master_port,
                  const char* log_file, const char* log_pos);

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
    std::vector<std::string> get_all_server_ids_str();

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
     * Checks that an SSL connection can be created to the node
     *
     * @return True if an encrypted connection to the database was created
     */
    bool check_ssl(int node);

    /**
     * Disables the server SSL configuration
     */
    void disable_ssl();

    /**
     * @brief Synchronize slaves with the master
     *
     * Only works with master-slave replication and should not be used with Galera clusters.
     * The function expects that the first node, @c nodes[0], is the master.
     */
    virtual void sync_slaves(int node = 0);

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

    // Replicates from a host and a port instead of a known server
    void replicate_from(int slave, const std::string& host, uint16_t port, const char* type = "current_pos");

    /**
     * @brief limit_nodes Restart replication for only new_N nodes
     * @param new_N new number of nodes in replication
     */
    void limit_nodes(int new_N);

    /**
     * @brief cnf_servers Generates backend servers description for maxscale.cnf
     * @return Servers description including IPs, ports
     */
    virtual std::string cnf_servers();

    /**
     * @brief cnf_servers_line Generates list of backend servers for serivces definition in maxscale.cnf
     * @return List of servers, e.g server1,server2,server3,...
     */
    std::string cnf_servers_line();

    /**
     * @brief cnf_server_name Prefix for backend server name ('server', 'gserver')
     */
    std::string cnf_server_name;

    bool using_ipv6() const;

protected:
    std::string m_test_dir; /**< path to test application */

private:
    Type m_type;
    bool m_use_ipv6 {false}; /**< Default to ipv6-addresses */

    bool check_master_node(MYSQL* conn);
    bool bad_slave_thread_status(MYSQL* conn, const char* field, int node);
};

class Galera_nodes : public Mariadb_nodes
{
public:

    Galera_nodes(SharedData& shared, const std::string& network_config)
        : Mariadb_nodes("galera", shared, network_config, Type::GALERA)
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

    virtual void sync_slaves(int node = 0)
    {
        sleep(10);
    }
};

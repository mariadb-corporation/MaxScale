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

class MariaDBCluster;

namespace maxtest
{
class MariaDB;
class TestLogger;

class MariaDBServer
{
    friend class ::MariaDBCluster;
public:
    using SMariaDB = std::unique_ptr<mxt::MariaDB>;
    MariaDBServer(const std::string& cnf_name, VMNode& vm, MariaDBCluster& cluster, int ind);

    bool start_database();
    bool stop_database();
    bool cleanup_database();

    std::string version_as_string();

    struct Status
    {
        uint64_t version_num {0};
        int64_t  server_id {-1};
        bool     read_only {false};
    };

    /**
     * Try to open a connection to the server. Failure is not a test error.
     * Uses the "skysql"-user similar to many tests.
     *
     * @return The connection. Success can be checked by 'is_open'.
     */
    SMariaDB try_open_connection();

    enum class SslMode {ON, OFF};
    SMariaDB try_open_connection(SslMode ssl);

    mxt::MariaDB* admin_connection();

    bool ping_or_open_admin_connection();

    bool               update_status();
    const Status&      status() const;
    const std::string& cnf_name() const;

    VMNode& vm_node();
    int     port();

private:
    Status   m_status;
    SMariaDB m_admin_conn;      /**< Admin-level connection to server. Usually kept open. */

    struct Settings
    {
        std::string start_db_cmd;   /**< Command to start DB server process */
        std::string stop_db_cmd;    /**< Command to stop DB server process */
        std::string cleanup_db_cmd; /**< Command to remove all data files */
    };

    const std::string m_cnf_name;   /**< MaxScale config name of server */
    Settings          m_settings;
    VMNode&           m_vm;
    MariaDBCluster&   m_cluster;
    const int         m_ind {-1};
};
}

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
class MariaDBCluster : public Nodes
{
public:
    using ConnArray = std::vector<std::unique_ptr<mxt::MariaDB>>;

    static constexpr int N_MAX = 32;
    virtual ~MariaDBCluster();

    void set_use_ipv6(bool use_ipv6);
    void set_use_ssl(bool use_ssl);

    const char* ip(int i = 0) const;
    const char* ip4(int i = 0) const;
    const char* ip6(int i = 0) const;
    const char* ip_private(int i = 0) const;
    const char* access_homedir(int i = 0) const;
    const char* access_sudo(int i = 0) const;

    /**
     * Return network config prefix of cluster items. Likely 'node', 'galera' or 'xpand'.
     *
     * @return Network config prefix
     */
    virtual const std::string& nwconf_prefix() const = 0;

    /**
     * Return readable cluster name. Used in log messages.
     *
     * @return Cluster name
     */
    virtual const std::string& name() const = 0;

    int N {0};

    MYSQL* nodes[N_MAX] {}; /**< MYSQL structs for every backend node */
    int    port[N_MAX];     /**< MariaDB port for every backend node */

    std::string user_name;  /**< User name to access backend nodes */
    std::string password;   /**< Password to access backend nodes */

    int connect(int i, const std::string& db = "test");
    int connect(const std::string& db = "test");

    /**
     * Get a Connection to a node
     */
    Connection get_connection(int i, const std::string& db = "test")
    {
        return Connection(ip4(i), port[i], user_name, password, db, m_ssl);
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
     * @brief  prints all nodes information
     * @return 0
     */
    void print_env();

    /**
     * Start mysqld on all nodes.
     *
     * @return  True on success
     */
    bool start_nodes();

    /**
     * Stop mysqld on all nodes.
     *
     * @return  True on success
     */
    bool stop_nodes();

    /**
     * @brief stop_slaves isues 'stop slave;' to all nodes
     * @return  0 in case of success
     */
    int stop_slaves();

    /**
     * Start replication in manner relevant to the cluster.
     *
     * @return  True on success
     */
    virtual bool start_replication() = 0;

    /**
     * Create the default users used by all tests
     */
    void create_users(int node);

    /**
     * Blocks `src` from communicating with `dest`
     */
    void block_node_from_node(int src, int dest);

    /**
     * Unblocks the block added by block_node_from_node
     */
    void unblock_node_from_node(int src, int dest);

    /**
     * Setup firewall on a backend node to block MariaDB port.
     *
     * @param node Index of node to block
     * @return True on success
     */
    bool block_node(int node);

    /**
     * Setup firewall on a backend node to allow MariaDB port.
     *
     * @param node Index of node to unblock
     * @return True on success
     */
    bool unblock_node(int node);


    /**
     * @brief Block all nodes for this cluster
     * @return 0 in case of success
     */
    int block_all_nodes();

    /**
     * Unblock all nodes for this cluster.
     *
     * @return True on success
     */
    bool unblock_all_nodes();

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
    std::vector<int>         get_all_server_ids();
    std::vector<std::string> get_all_server_ids_str();

    /**
     * Initializes and tests ssh-connection and removes some logs. If this fails, VMs are seriously wrong
     * and continuing is pointless. Does not communicate with the server processes.
     *
     * @return True on success
     */
    bool basic_test_prepare();

    bool prepare_servers_for_test();

    /**
     * @brief Execute query on all nodes
     * @param sql query to execute
     * @return 0 in case of success
     */
    int execute_query_all_nodes(const char* sql);

    /**
     * Disables the server SSL configuration
     */
    void disable_ssl();

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
     * Get the server configuration file name for a VM node. E.g. server1.cnf.
     *
     * @param node Node number for which the configuration is requested
     * @return The name of the configuration file
     */
    virtual std::string get_srv_cnf_filename(int node) = 0;

    /**
     * Restore the original configuration for all servers.
     */
    void reset_all_servers_settings();

    // Same but for an individual server
    void reset_server_settings(int node);

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

    const std::string& cnf_server_prefix() const;

    /**
     * Get cluster type as string. The returned value is given to create_user.sh and should match one
     * of the expected values.
     *
     * @return Cluster type
     */
    virtual const std::string& type_string() const = 0;

    bool setup(const mxt::NetworkConfig& nwconfig, int n_min_expected);
    bool update_status();
    bool check_backend_versions(uint64_t min_version);
    bool check_create_test_db();

    mxt::MariaDBServer* backend(int i);

    /**
     * Ping or open admin connections to all servers.
     *
     * @return Number of succesfull connections
     */
    int ping_or_open_admin_connections();

    bool ssl() const;
    bool using_ipv6() const;

protected:
    /**
     * Constructor
     *
     * @param shared Global data
     * @param cnf_server_prefix Node prefix in MaxScale config file
     */
    MariaDBCluster(mxt::SharedData* shared, const std::string& cnf_server_prefix);

    /**
     * @returns SELECT that returns anonymous users in such a way that each returned row
     *          can directly be given as argument to DROP USER.
     */
    virtual std::string anonymous_users_query() const;

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

    std::string extract_version_from_string(const std::string& version);

    mxt::TestLogger& logger();

    std::string m_test_dir;             /**< path to test application */
    /**< Prefix for backend server name in MaxScale config. E.g. 'server', 'gserver' */
    std::string m_cnf_server_prefix;
    std::string m_socket_cmd[N_MAX];    /**< 'socket=$socket' line */

private:
    bool m_use_ipv6 {false};    /**< Default to ipv6-addresses */
    bool m_ssl {false};         /**< Use ssl? */
    bool m_blocked[N_MAX] {};   /**< List of blocked nodes */

    std::vector<std::unique_ptr<mxt::MariaDBServer>> m_backends;

    int  read_nodes_info(const mxt::NetworkConfig& nwconfig);
    bool reset_servers();
    bool run_on_every_backend(const std::function<bool(int)>& func);

    /**
     * Check if the cluster is replicating or otherwise properly synced. May also attempt light fixes.
     * Should not wipe out database contents etc.
     *
     * @return True if cluster is ready for test
     */
    virtual bool check_replication() = 0;

    /**
     * Initialize MariaDB setup (run mysql_install_db).
     *
     * @param i Node index
     * @return True on success
     */
    virtual bool reset_server(int i);

    bool check_normal_conns();
};

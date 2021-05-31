#pragma once

#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <maxtest/ccdefs.hh>
#include <maxtest/mariadb_func.hh>
#include <maxtest/mariadb_nodes.hh>
#include <maxtest/nodes.hh>

namespace maxtest
{
class MariaDB;
}

class Maxscales : private Nodes
{
public:
    enum service
    {
        RWSPLIT,
        READCONN_MASTER,
        READCONN_SLAVE
    };

    Maxscales(mxt::SharedData* shared);

    ~Maxscales();

    bool setup(const mxt::NetworkConfig& nwconfig, const std::string& vm_name);

    void set_use_ipv6(bool use_ipv6);
    void set_ssl(bool ssl);

    const char* ip4() const;
    const char* ip() const;
    const char* ip_private() const;
    const char* hostname() const;

    const char* access_user() const;
    const char* access_homedir() const;
    const char* access_sudo() const;
    const char* sshkey() const;

    static const std::string& prefix();
    const std::string&        node_name() const;

    bool ssl() const;

    int rwsplit_port {-1};          /**< RWSplit port */
    int readconn_master_port {-1};  /**< ReadConnection in master mode port */
    int readconn_slave_port {-1};   /**< ReadConnection in slave mode port */

    /**
     * @brief Get port number of a MaxScale service
     *
     * @param type Type of service
     * @return Port number of the service
     */
    int port(enum service type = RWSPLIT) const;

    MYSQL* conn_rwsplit[1] {nullptr};   /**< Connection to RWSplit */
    MYSQL* conn_master {nullptr};       /**< Connection to ReadConnection in master mode */
    MYSQL* conn_slave {nullptr};        /**< Connection to ReadConnection in slave mode */

    /**< conn_rwsplit, conn_master, conn_slave */
    MYSQL* routers[3] {nullptr, nullptr, nullptr};
    int    ports[3] {-1, -1, -1};           /**< rwsplit_port, readconn_master_port, readconn_slave_port */

    std::string maxscale_cnf;       /**< full name of Maxscale configuration file */
    std::string maxscale_log_dir;   /**< name of log files directory */

    std::string user_name;  /**< User name to access backend nodes */
    std::string password;   /**< Password to access backend nodes */

    /**
     * @brief ConnectMaxscale   Opens connections to RWSplit, ReadConn master and ReadConn slave Maxscale
     * services
     * Opens connections to RWSplit, ReadConn master and ReadConn slave Maxscale services
     * Connections stored in maxscales->conn_rwsplit[0], maxscales->conn_master[0] and
     * maxscales->conn_slave[0] MYSQL structs
     * @return 0 in case of success
     */
    int connect_maxscale(const std::string& db = "test");
    int connect(const std::string& db = "test");

    /**
     * @brief CloseMaxscaleConn Closes connection that were opened by ConnectMaxscale()
     * @return 0
     */
    int close_maxscale_connections();
    int disconnect();

    /**
     * @brief ConnectRWSplit    Opens connections to RWSplit and store MYSQL struct in
     * maxscales->conn_rwsplit[0]
     * @return 0 in case of success
     */
    int connect_rwsplit(const std::string& db = "test");

    /**
     * @brief ConnectReadMaster Opens connections to ReadConn master and store MYSQL struct in
     * maxscales->conn_master[0]
     * @return 0 in case of success
     */
    int connect_readconn_master(const std::string& db = "test");

    /**
     * @brief ConnectReadSlave Opens connections to ReadConn slave and store MYSQL struct in
     * maxscales->conn_slave[0]
     * @return 0 in case of success
     */
    int connect_readconn_slave(const std::string& db = "test");

    /**
     * @brief OpenRWSplitConn   Opens new connections to RWSplit and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return MYSQL struct
     */
    MYSQL* open_rwsplit_connection(const std::string& db = "test");

    /**
     * Get a readwritesplit Connection
     */
    Connection rwsplit(const std::string& db = "test");

    /**
     * Get a Connection to a specific port
     */
    Connection get_connection(int port, const std::string& db = "test");

    /**
     * @brief OpenReadMasterConn    Opens new connections to ReadConn master and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return MYSQL struct
     */
    MYSQL* open_readconn_master_connection();

    /**
     * Get a readconnroute master Connection
     */
    Connection readconn_master(const std::string& db = "test");

    /**
     * @brief OpenReadSlaveConn    Opens new connections to ReadConn slave and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return  MYSQL struct
     */
    MYSQL* open_readconn_slave_connection();

    /**
     * Get a readconnroute slave Connection
     */
    Connection readconn_slave(const std::string& db = "test");

    /**
     * @brief CloseRWSplit Closes RWplit connections stored in maxscales->conn_rwsplit[0]
     */
    void close_rwsplit();

    /**
     * @brief CloseReadMaster Closes ReadConn master connections stored in maxscales->conn_master[0]
     */
    void close_readconn_master();

    /**
     * @brief restart_maxscale Issues 'service maxscale restart' command
     */
    int restart_maxscale();
    int restart();

    /**
     * Issues 'service maxscale start' command
     */
    int start_maxscale();
    int start();

    /**
     * @brief stop_maxscale Issues 'service maxscale stop' command
     */
    int  stop_maxscale();
    bool stop();

    bool start_and_check_started();
    bool stop_and_check_stopped();

    /**
     * Execute a MaxCtrl command
     *
     * @param cmd  Command to execute, without the `maxctrl` part
     * @param sudo Run the command as root
     *
     * @return The exit code and output of MaxCtrl
     */
    mxt::CmdResult maxctrl(const std::string& cmd, bool sudo = true);

    /**
     * @brief get_maxscale_memsize Gets size of the memory consumed by Maxscale process
     * @param m Number of Maxscale node
     * @return memory size in kilobytes
     */
    long unsigned get_maxscale_memsize(int m = 0);

    void copy_log(int i, double timestamp, const std::string& test_name);

    /**
     * @brief Get the set of labels that are assigned to server @c name
     *
     * @param name The name of the server
     * @return A set of string labels assigned to this server
     */
    StringSet get_server_status(const std::string& name);

    /**
     * Wait until the monitors have performed at least one monitoring operation
     *
     * The function waits until all monitors have performed at least one monitoring cycle.
     *
     * @param intervals The number of monitor intervals to wait
     */
    void wait_for_monitor(int intervals = 2);

    mxt::CmdResult ssh_output(const std::string& cmd, bool sudo = true);

    int ssh_node(const std::string& cmd, bool sudo);
    int ssh_node_f(int node, bool sudo, const char* format, ...) mxb_attribute((format(printf, 4, 5)));
    int copy_to_node(const char* src, const char* dest);
    int copy_from_node(const char* src, const char* dest);

    /**
     * Copy rules file for firewall filter to MaxScale machine.
     *
     * @param rules_name Rule file source filename
     * @param rules_dir Rule file source directory
     */
    void copy_fw_rules(const std::string& rules_name, const std::string& rules_dir);

    /**
     * Check if MaxScale process is running or stopped. Wrong status is a test failure.
     *
     * @param expected True if expected to be running
     */
    void expect_running_status(bool expected);

    bool reinstall(const std::string& target, const std::string& mdbci_config_name);

    bool use_valgrind() const;
    bool prepare_for_test();
    void write_env_vars();

    mxt::VMNode& vm_node();

private:
    bool m_use_ipv6 {false};    /**< Default to ipv6-addresses */
    bool m_ssl {false};         /**< Use ssl when connecting to MaxScale */

    int  m_valgrind_log_num {0};    /**< Counter for Maxscale restarts to avoid Valgrind log overwriting */
    bool m_use_valgrind {false};    /**< Run MaxScale under Valgrind? */
    bool m_use_callgrind {false};   /**< Run MaxScale under Valgrind with --callgrind option */

    std::string m_binlog_dir;   /**< Directory of binlog files (for binlog router) */

    mxt::TestLogger& log() const;
};

class TestConnections;

namespace maxtest
{
class TestLogger;

/**
 * Contains information about one server as seen by MaxScale.
 */
struct ServerInfo
{
    using bitfield = uint32_t;
    static constexpr bitfield UNKNOWN = 0;
    static constexpr bitfield RUNNING = (1 << 0);
    static constexpr bitfield MASTER = (1 << 1);
    static constexpr bitfield SLAVE = (1 << 2);
    static constexpr bitfield RELAY = (1 << 3);
    static constexpr bitfield SERVER_SLAVE_OF_EXT_MASTER = (1 << 10);
    static constexpr bitfield BLR = (1 << 12);
    static constexpr bitfield DOWN = (1 << 13);

    static constexpr bitfield master_st = MASTER | RUNNING;
    static constexpr bitfield slave_st = SLAVE | RUNNING;

    static constexpr int GROUP_NONE = -1;
    static constexpr int RLAG_NONE = -1;
    static constexpr int SRV_ID_NONE = -1;

    static std::string status_to_string(bitfield status);
    std::string        status_to_string() const;
    void               status_from_string(const std::string& source);
    std::string        to_string_short() const;

    std::string name {"<unknown>"}; /**< Server name */
    bitfield    status {UNKNOWN};   /**< Status bitfield */
    int64_t     server_id {SRV_ID_NONE};
    int64_t     master_group {GROUP_NONE};
    int64_t     rlag {RLAG_NONE};
    int64_t     pool_conns {0};
    int64_t     connections {0};
    std::string gtid;

    struct SlaveConnection
    {
        std::string name;
        std::string gtid;
        int64_t     master_id {SRV_ID_NONE};

        enum class IO_State
        {
            NO,
            CONNECTING,
            YES
        };
        IO_State io_running {IO_State::NO};
        bool     sql_running {false};
    };

    std::vector<SlaveConnection> slave_connections;
};

/**
 * Contains information about multiple servers as seen by MaxScale.
 */
class ServersInfo
{
public:
    ServersInfo(TestLogger* log);

    ServersInfo(const ServersInfo& rhs) = default;
    ServersInfo& operator=(const ServersInfo& rhs);
    ServersInfo(ServersInfo&& rhs) noexcept;
    ServersInfo& operator=(ServersInfo&& rhs) noexcept;

    void add(const ServerInfo& info);
    void add(ServerInfo&& info);

    const ServerInfo& get(size_t i) const;
    ServerInfo        get(const std::string& cnf_name) const;
    size_t            size() const;

    /**
     * Return the server info of the master. If no masters are found, returns a default server info object.
     * If multiple masters are found, returns the first.
     *
     * @return Server info of master.
     */
    ServerInfo get_master() const;

    /**
     * Check that server status is as expected. Increments global error counter if differences found.
     *
     * @param expected_status Expected server statuses. Each status should be a bitfield of values defined
     * in the ServerInfo-class.
     */
    void check_servers_status(const std::vector<ServerInfo::bitfield>& expected_status);

    void check_master_groups(const std::vector<int>& expected_groups);
    void check_pool_connections(const std::vector<int>& expected_conns);
    void check_connections(const std::vector<int>& expected_conns);

    void print();

    /**
     * Get starting server states for a master-slave cluster: master + 3 slaves.
     */
    static const std::vector<ServerInfo::bitfield>& default_repl_states();

private:
    std::vector<ServerInfo> m_servers;
    TestLogger*             m_log {nullptr};

    void check_servers_property(size_t n_expected, const std::function<void(size_t)>& tester);
};

class MaxScale
{
public:
    MaxScale(const MaxScale& rhs) = delete;
    MaxScale& operator=(const MaxScale& rhs) = delete;

    MaxScale(Maxscales* maxscales, SharedData& shared);

    /**
     * Wait for monitors to tick.
     *
     * @param ticks The number of monitor ticks to wait
     */
    void wait_monitor_ticks(int ticks = 1);

    /**
     * Get servers info.
     *
     * @return Server info object
     */
    ServersInfo get_servers();

    /**
     * Check that server status is as expected. Increments global error counter if differences found.
     *
     * @param expected_status Expected server statuses. Each status should be a bitfield of values defined
     * in the ServerInfo-class.
     */
    void check_servers_status(const std::vector<ServerInfo::bitfield>& expected_status);

    void alter_monitor(const std::string& mon_name, const std::string& setting, const std::string& value);

    /**
     * Run a custom MaxCtrl command.
     *
     * @param cmd The command.
     * @return Result structure
     */
    mxt::CmdResult maxctrl(const std::string& cmd);

    const std::string& name() const;

    void start();
    void stop();
    void delete_log();

    std::unique_ptr<mxt::MariaDB> open_rwsplit_connection(const std::string& db = "");

private:
    Maxscales* const m_maxscales {nullptr};
    SharedData&      m_shared;

    std::string m_rest_user {"admin"};
    std::string m_rest_pw {"mariadb"};
    std::string m_rest_ip {"127.0.0.1"};
    std::string m_rest_port {"8989"};

    mxt::CmdResult   curl_rest_api(const std::string& path);
    mxt::TestLogger& logger();
};
}

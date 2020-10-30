#pragma once

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

class Maxscales : public Nodes
{
public:
    static const int MAX_MAXSCALES = 256;

    enum service
    {
        RWSPLIT,
        READCONN_MASTER,
        READCONN_SLAVE
    };

    Maxscales(const char* pref, const char* test_cwd, bool verbose,
              const std::string& network_config);

    ~Maxscales();

    bool setup() override;

    int  read_env();
    void set_use_ipv6(bool use_ipv6);

    const char* ip4(int i = 0) const;
    const char* ip(int i = 0) const;

    const char* hostname(int i = 0) const;

    const char* access_user(int i = 0) const;
    const char* access_homedir(int i = 0) const;
    const char* access_sudo(int i = 0) const;
    const char* sshkey(int i = 0) const;

    const std::string& prefix() const;

    /**
     * @brief rwsplit_port RWSplit service port
     */
    int rwsplit_port[MAX_MAXSCALES];

    /**
     * @brief readconn_master_port ReadConnection in master mode service port
     */
    int readconn_master_port[MAX_MAXSCALES] {};

    /**
     * @brief readconn_slave_port ReadConnection in slave mode service port
     */
    int readconn_slave_port[MAX_MAXSCALES] {};

    /**
     * @brief Get port number of a MaxScale service
     *
     * @param type Type of service
     * @param m    MaxScale instance to use
     *
     * @return Port number of the service
     */
    int port(enum service type = RWSPLIT, int m = 0) const;

    /**
     * @brief binlog_port binlog router service port
     */
    int binlog_port[MAX_MAXSCALES] {};

    /**
     * @brief conn_rwsplit  MYSQL connection struct to RWSplit service
     */
    MYSQL* conn_rwsplit[MAX_MAXSCALES] {};

    /**
     * @brief conn_master   MYSQL connection struct to ReadConnection in master mode service
     */
    MYSQL* conn_master[MAX_MAXSCALES] {};

    /**
     * @brief conn_slave MYSQL connection struct to ReadConnection in slave mode service
     */
    MYSQL* conn_slave[MAX_MAXSCALES] {};

    /**
     * @brief routers Array of 3 MYSQL handlers which contains copies of conn_rwsplit, conn_master, conn_slave
     */
    MYSQL* routers[MAX_MAXSCALES][3] {};

    /**
     * @brief ports of 3 int which contains copies of rwsplit_port, readconn_master_port, readconn_slave_port
     */
    int ports[MAX_MAXSCALES][3] {};

    /**
     * @brief maxscale_cnf full name of Maxscale configuration file
     */
    std::string maxscale_cnf[MAX_MAXSCALES];

    /**
     * @brief maxscale_log_dir name of log files directory
     */
    std::string maxscale_log_dir[MAX_MAXSCALES];

    /**
     * @brief maxscale_lbinog_dir name of binlog files (for binlog router) directory
     */
    std::string maxscale_binlog_dir[MAX_MAXSCALES];

    /**
     * @brief N_ports Default number of routers
     */
    int N_ports[MAX_MAXSCALES] {};

    /**
     * @brief test_dir path to test application
     */
    char test_dir[4096] {};

    bool ssl = false;

    /**
     * @brief   User name to access backend nodes
     */
    std::string user_name;
    /**
     * @brief   Password to access backend nodes
     */
    std::string password;

    /**
     * @brief ConnectMaxscale   Opens connections to RWSplit, ReadConn master and ReadConn slave Maxscale
     * services
     * Opens connections to RWSplit, ReadConn master and ReadConn slave Maxscale services
     * Connections stored in maxscales->conn_rwsplit[0], maxscales->conn_master[0] and
     * maxscales->conn_slave[0] MYSQL structs
     * @return 0 in case of success
     */
    int connect_maxscale(int m = 0, const std::string& db = "test");
    int connect(int m = 0, const std::string& db = "test")
    {
        return connect_maxscale(m, db);
    }

    /**
     * @brief CloseMaxscaleConn Closes connection that were opened by ConnectMaxscale()
     * @return 0
     */
    int close_maxscale_connections(int m = 0);
    int disconnect(int m = 0)
    {
        return close_maxscale_connections(m);
    }

    /**
     * @brief ConnectRWSplit    Opens connections to RWSplit and store MYSQL struct in
     * maxscales->conn_rwsplit[0]
     * @return 0 in case of success
     */
    int connect_rwsplit(int m = 0, const std::string& db = "test");

    /**
     * @brief ConnectReadMaster Opens connections to ReadConn master and store MYSQL struct in
     * maxscales->conn_master[0]
     * @return 0 in case of success
     */
    int connect_readconn_master(int m = 0, const std::string& db = "test");

    /**
     * @brief ConnectReadSlave Opens connections to ReadConn slave and store MYSQL struct in
     * maxscales->conn_slave[0]
     * @return 0 in case of success
     */
    int connect_readconn_slave(int m = 0, const std::string& db = "test");

    /**
     * @brief OpenRWSplitConn   Opens new connections to RWSplit and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return MYSQL struct
     */
    MYSQL* open_rwsplit_connection(int m = 0, const std::string& db = "test")
    {
        return open_conn(rwsplit_port[m], ip4(m), user_name, password, ssl);
    }

    /**
     * Get a readwritesplit Connection
     */
    Connection rwsplit(int m = 0, const std::string& db = "test")
    {
        return Connection(ip4(m), rwsplit_port[m], user_name, password, db, ssl);
    }

    /**
     * Get a Connection to a specific port
     */
    Connection get_connection(int port, int m = 0, const std::string& db = "test")
    {
        return Connection(ip4(m), port, user_name, password, db, ssl);
    }

    /**
     * @brief OpenReadMasterConn    Opens new connections to ReadConn master and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return MYSQL struct
     */
    MYSQL* open_readconn_master_connection(int m = 0)
    {
        return open_conn(readconn_master_port[m], ip4(m), user_name, password, ssl);
    }

    /**
     * Get a readconnroute master Connection
     */
    Connection readconn_master(int m = 0, const std::string& db = "test")
    {
        return Connection(ip4(m), readconn_master_port[m], user_name, password, db, ssl);
    }

    /**
     * @brief OpenReadSlaveConn    Opens new connections to ReadConn slave and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return  MYSQL struct
     */
    MYSQL* open_readconn_slave_connection(int m = 0)
    {
        return open_conn(readconn_slave_port[m], ip4(m), user_name, password, ssl);
    }

    /**
     * Get a readconnroute slave Connection
     */
    Connection readconn_slave(int m = 0, const std::string& db = "test")
    {
        return Connection(ip4(m), readconn_slave_port[m], user_name, password, db, ssl);
    }

    /**
     * @brief CloseRWSplit Closes RWplit connections stored in maxscales->conn_rwsplit[0]
     */
    void close_rwsplit(int m = 0)
    {
        mysql_close(conn_rwsplit[m]);
        conn_rwsplit[m] = NULL;
    }

    /**
     * @brief CloseReadMaster Closes ReadConn master connections stored in maxscales->conn_master[0]
     */
    void close_readconn_master(int m = 0)
    {
        mysql_close(conn_master[m]);
        conn_master[m] = NULL;
    }

    /**
     * @brief CloseReadSlave Closes ReadConn slave connections stored in maxscales->conn_slave[0]
     */
    void close_readconn_slave(int m = 0)
    {
        mysql_close(conn_slave[m]);
        conn_slave[m] = NULL;
    }

    /**
     * @brief restart_maxscale Issues 'service maxscale restart' command
     */
    int restart_maxscale(int m = 0);
    int restart(int m = 0)
    {
        return restart_maxscale(m);
    }

    /**
     * @brief alias for restart_maxscale
     */
    int start_maxscale(int m = 0);

    int start(int m = 0)
    {
        return start_maxscale(m);
    }

    /**
     * @brief stop_maxscale Issues 'service maxscale stop' command
     */
    int stop_maxscale(int m = 0);
    int stop(int m = 0)
    {
        return stop_maxscale(m);
    }

    // Helper for stopping all maxscales
    void stop_all()
    {
        std::vector<std::thread> thr;

        for (int i = 0; i < N; i++)
        {
            thr.emplace_back([this, i]() {
                                 stop(i);
                             });
        }

        for (auto& a : thr)
        {
            a.join();
        }
    }
    /**
     * Execute a MaxCtrl command
     *
     * @param cmd  Command to execute, without the `maxctrl` part
     * @param m    MaxScale node to execute the command on
     * @param sudo Run the command as root
     *
     * @return The exit code and output of MaxCtrl
     */
    SshResult maxctrl(const std::string& cmd, int m = 0, bool sudo = true)
    {
        return ssh_output("maxctrl " + cmd, m, sudo);
    }

    /**
     * @brief get_maxscale_memsize Gets size of the memory consumed by Maxscale process
     * @param m Number of Maxscale node
     * @return memory size in kilobytes
     */
    long unsigned get_maxscale_memsize(int m = 0);

    /**
     * @brief Get the set of labels that are assigned to server @c name
     *
     * @param name The name of the server
     *
     * @param m Number of Maxscale node
     *
     * @return A set of string labels assigned to this server
     */
    StringSet get_server_status(const std::string& name, int m = 0);

    /**
     * Wait until the monitors have performed at least one monitoring operation
     *
     * The function waits until all monitors have performed at least one monitoring cycle.
     *
     * @param intervals The number of monitor intervals to wait
     * @param m Number of Maxscale node
     */
    void wait_for_monitor(int intervals = 2, int m = 0);

    /**
     * @brief use_valrind if true Maxscale will be executed under Valgrind
     */
    bool use_valgrind {false};

    /**
     * @brief use_callgrind if true Maxscale will be executed under Valgrind with
     * --callgrind option
     */
    bool use_callgrind {false};

    /**
     * @brief valgring_log_num Counter for Maxscale restarts to avoid Valgrind log overwriting
     */
    int valgring_log_num {0};

private:
    bool m_use_ipv6 {false};    /**< Default to ipv6-addresses */
};

class TestConnections;
class TestLogger;

namespace maxtest
{

/**
 * Contains information about one server as seen by MaxScale.
 */
struct ServerInfo
{
    using bitfield = uint32_t;
    static constexpr bitfield DOWN = 0;
    static constexpr bitfield RUNNING = (1 << 0);
    static constexpr bitfield MASTER = (1 << 1);
    static constexpr bitfield SLAVE = (1 << 2);
    static constexpr bitfield RELAY = (1 << 3);
    static constexpr bitfield SERVER_SLAVE_OF_EXT_MASTER = (1 << 10);
    static constexpr bitfield BLR = (1 << 12);

    static constexpr bitfield master_st = MASTER | RUNNING;
    static constexpr bitfield slave_st = SLAVE | RUNNING;

    static constexpr int GROUP_NONE = -1;
    static constexpr int RLAG_NONE = -1;
    static constexpr int SRV_ID_NONE = -1;

    static std::string status_to_string(bitfield status);
    std::string        status_to_string() const;
    void               status_from_string(const std::string& source);

    std::string name {"<unknown>"}; /**< Server name */
    bitfield    status {0};         /**< Status bitfield */
    int64_t     server_id {SRV_ID_NONE};
    int64_t     master_group {GROUP_NONE};
    int64_t     rlag {RLAG_NONE};

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
    ServersInfo(TestLogger& log);

    ServersInfo(const ServersInfo& rhs) = default;
    ServersInfo& operator=(const ServersInfo& rhs);
    ServersInfo(ServersInfo&& rhs) noexcept;
    ServersInfo& operator=(ServersInfo&& rhs) noexcept;

    void add(const ServerInfo& info);
    void add(ServerInfo&& info);

    const ServerInfo& get(size_t i) const;
    size_t            size() const;

    /**
     * Check that server status is as expected. Increments global error counter if differences found.
     *
     * @param expected_status Expected server statuses. Each status should be a bitfield of values defined
     * in the ServerInfo-class.
     */
    void check_servers_status(const std::vector<ServerInfo::bitfield>& expected_status);

    void check_master_groups(const std::vector<int>& expected_groups);

private:
    std::vector<ServerInfo> m_servers;
    TestLogger&             m_log;
};

class MaxScale
{
public:
    MaxScale(const MaxScale& rhs) = delete;
    MaxScale& operator=(const MaxScale& rhs) = delete;

    MaxScale(Maxscales* maxscales, TestLogger& log, int node_ind);

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

    void start();
    void stop();

    std::unique_ptr<mxt::MariaDB> open_rwsplit_connection(const std::string& db = "");

private:
    Maxscales* const m_maxscales {nullptr};
    TestLogger&      m_log;
    int              m_node_ind {-1};       /**< Node index of this MaxScale */

    std::string m_rest_user {"admin"};
    std::string m_rest_pw {"mariadb"};
    std::string m_rest_ip {"127.0.0.1"};
    std::string m_rest_port {"8989"};

    Nodes::SshResult curl_rest_api(const std::string& path);
};
}

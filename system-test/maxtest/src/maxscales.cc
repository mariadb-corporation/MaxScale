#include <maxtest/maxscales.hh>
#include <string>
#include <maxbase/format.hh>
#include <maxbase/jansson.h>
#include <maxbase/json.hh>
#include <maxbase/string.hh>
#include <maxtest/envv.hh>
#include <maxtest/log.hh>
#include <maxtest/mariadb_connector.hh>
#include <maxtest/testconnections.hh>

#define DEFAULT_MAXSCALE_CNF        "/etc/maxscale.cnf"
#define DEFAULT_MAXSCALE_LOG_DIR    "/var/log/maxscale/"
#define DEFAULT_MAXSCALE_BINLOG_DIR "/var/lib/maxscale/Binlog_Service/"

using std::string;

Maxscales::Maxscales(const char* pref,
                     const char* test_cwd,
                     bool verbose,
                     const std::string& network_config)
    : Nodes(pref, network_config, verbose)
    , valgring_log_num(0)
{
    strcpy(this->test_dir, test_cwd);
}

Maxscales::~Maxscales()
{
    for (int i = 0; i < MAX_MAXSCALES; ++i)
    {
        close_maxscale_connections(i);
    }
}

bool Maxscales::setup()
{
    read_env();     // Sets e.g. use_valgrind.
    Nodes::init_ssh_masters();

    if (this->use_valgrind)
    {
        for (int i = 0; i < N; i++)
        {
            ssh_node_f(i, true, "yum install -y valgrind gdb 2>&1");
            ssh_node_f(i, true, "apt install -y --force-yes valgrind gdb 2>&1");
            ssh_node_f(i, true, "zypper -n install valgrind gdb 2>&1");
            ssh_node_f(i, true, "rm -rf /var/cache/maxscale/maxscale.lock");
        }
    }
    return true;
}

int Maxscales::read_env()
{
    char env_name[64];

    read_basic_env();

    auto prefixc = prefix().c_str();
    sprintf(env_name, "%s_user", prefixc);
    user_name = readenv(env_name, "skysql");

    sprintf(env_name, "%s_password", prefixc);
    password = readenv(env_name, "skysql");

    if ((N > 0) && (N < 255))
    {
        for (int i = 0; i < N; i++)
        {
            sprintf(env_name, "%s_%03d_cnf", prefixc, i);
            maxscale_cnf[i] = readenv(env_name, DEFAULT_MAXSCALE_CNF);

            sprintf(env_name, "%s_%03d_log_dir", prefixc, i);
            maxscale_log_dir[i] = readenv(env_name, DEFAULT_MAXSCALE_LOG_DIR);

            sprintf(env_name, "%s_%03d_binlog_dir", prefixc, i);
            maxscale_binlog_dir[i] = readenv(env_name, DEFAULT_MAXSCALE_BINLOG_DIR);

            rwsplit_port[i] = 4006;
            readconn_master_port[i] = 4008;
            readconn_slave_port[i] = 4009;
            binlog_port[i] = 5306;

            ports[i][0] = rwsplit_port[i];
            ports[i][1] = readconn_master_port[i];
            ports[i][2] = readconn_slave_port[i];

            N_ports[0] = 3;
        }
    }

    use_valgrind = readenv_bool("use_valgrind", false);
    use_callgrind = readenv_bool("use_callgrind", false);
    if (use_callgrind)
    {
        use_valgrind = true;
    }

    return 0;
}

int Maxscales::connect_rwsplit(int m, const std::string& db)
{
    mysql_close(conn_rwsplit[m]);

    conn_rwsplit[m] = open_conn_db(rwsplit_port[m], ip(m), db, user_name, password, ssl);
    routers[m][0] = conn_rwsplit[m];

    int rc = 0;
    int my_errno = mysql_errno(conn_rwsplit[m]);

    if (my_errno)
    {
        if (verbose)
        {
            printf("Failed to connect to readwritesplit: %d, %s\n", my_errno, mysql_error(conn_rwsplit[m]));
        }
        rc = my_errno;
    }

    return rc;
}

int Maxscales::connect_readconn_master(int m, const std::string& db)
{
    mysql_close(conn_master[m]);

    conn_master[m] = open_conn_db(readconn_master_port[m], ip(m), db, user_name, password, ssl);
    routers[m][1] = conn_master[m];

    int rc = 0;
    int my_errno = mysql_errno(conn_master[m]);

    if (my_errno)
    {
        if (verbose)
        {
            printf("Failed to connect to readwritesplit: %d, %s\n", my_errno, mysql_error(conn_master[m]));
        }
        rc = my_errno;
    }

    return rc;
}

int Maxscales::connect_readconn_slave(int m, const std::string& db)
{
    mysql_close(conn_slave[m]);

    conn_slave[m] = open_conn_db(readconn_slave_port[m], ip(m), db, user_name, password, ssl);
    routers[m][2] = conn_slave[m];

    int rc = 0;
    int my_errno = mysql_errno(conn_slave[m]);

    if (my_errno)
    {
        if (verbose)
        {
            printf("Failed to connect to readwritesplit: %d, %s\n", my_errno, mysql_error(conn_slave[m]));
        }
        rc = my_errno;
    }

    return rc;
}

int Maxscales::connect_maxscale(int m, const std::string& db)
{
    return connect_rwsplit(m, db)
           + connect_readconn_master(m, db)
           + connect_readconn_slave(m, db);
}

int Maxscales::close_maxscale_connections(int m)
{
    mysql_close(conn_master[m]);
    mysql_close(conn_slave[m]);
    mysql_close(conn_rwsplit[m]);

    conn_master[m] = nullptr;
    conn_slave[m] = nullptr;
    conn_rwsplit[m] = nullptr;
    return 0;
}

int Maxscales::restart_maxscale(int m)
{
    int res;
    if (use_valgrind)
    {
        res = stop_maxscale(m);
        res += start_maxscale(m);
    }
    else
    {
        res = ssh_node(m, "service maxscale restart", true);
    }
    fflush(stdout);
    return res;
}

int Maxscales::start_maxscale(int m)
{
    int res;
    if (use_valgrind)
    {
        if (use_callgrind)
        {
            res = ssh_node_f(m, false,
                             "sudo --user=maxscale valgrind -d "
                             "--log-file=/%s/valgrind%02d.log --trace-children=yes "
                             " --tool=callgrind --callgrind-out-file=/%s/callgrind%02d.log "
                             " /usr/bin/maxscale",
                             maxscale_log_dir[m].c_str(), valgring_log_num,
                             maxscale_log_dir[m].c_str(), valgring_log_num);
        }
        else
        {
            res = ssh_node_f(m, false,
                             "sudo --user=maxscale valgrind --leak-check=full --show-leak-kinds=all "
                             "--log-file=/%s/valgrind%02d.log --trace-children=yes "
                             "--track-origins=yes /usr/bin/maxscale",
                             maxscale_log_dir[m].c_str(), valgring_log_num);
        }
        valgring_log_num++;
    }
    else
    {
        res = ssh_node(m, "service maxscale restart", true);
    }
    fflush(stdout);
    return res;
}

int Maxscales::stop_maxscale(int m)
{
    int res;
    if (use_valgrind)
    {
        const char kill_vgrind[] = "kill $(pidof valgrind) 2>&1 > /dev/null";
        res = ssh_node(m, kill_vgrind, true);
        auto vgrind_pid = ssh_output("pidof valgrind", m);
        bool still_running = (atoi(vgrind_pid.output.c_str()) > 0);
        if ((res != 0) || still_running)
        {
            // Try again, maybe it will work.
            res = ssh_node(m, kill_vgrind, true);
        }
    }
    else
    {
        res = ssh_node(m, "service maxscale stop", true);
    }
    fflush(stdout);
    return res;
}

long unsigned Maxscales::get_maxscale_memsize(int m)
{
    auto res = ssh_output("ps -e -o pid,vsz,comm= | grep maxscale", m, false);
    long unsigned mem = 0;
    pid_t pid;
    sscanf(res.output.c_str(), "%d %lu", &pid, &mem);
    return mem;
}

StringSet Maxscales::get_server_status(const std::string& name, int m)
{
    StringSet rval;
    auto res = maxctrl("api get servers/" + name + " data.attributes.state", m);

    if (res.rc == 0 && res.output.length() > 2)
    {
        auto status = res.output.substr(1, res.output.length() - 2);

        for (const auto& a : mxb::strtok(status, ","))
        {
            rval.insert(mxb::trimmed_copy(a));
        }
    }

    return rval;
}

int Maxscales::port(enum service type, int m) const
{
    switch (type)
    {
    case RWSPLIT:
        return rwsplit_port[m];

    case READCONN_MASTER:
        return readconn_master_port[m];

    case READCONN_SLAVE:
        return readconn_slave_port[m];
    }
    return -1;
}

void Maxscales::wait_for_monitor(int intervals, int m)
{
    ssh_node_f(m, false,
               "for ((i=0;i<%d;i++)); do maxctrl api get maxscale/debug/monitor_wait; done",
               intervals);
}

const char* Maxscales::ip(int i) const
{
    return m_use_ipv6 ? Nodes::ip6(i) : Nodes::ip4(i);
}

void Maxscales::set_use_ipv6(bool use_ipv6)
{
    m_use_ipv6 = use_ipv6;
    this->use_ipv6 = use_ipv6;
}

const char* Maxscales::hostname(int i) const
{
    return Nodes::hostname(i);
}

const char* Maxscales::access_user(int i) const
{
    return Nodes::access_user(i);
}

const char* Maxscales::access_homedir(int i) const
{
    return Nodes::access_homedir(i);
}

const char* Maxscales::access_sudo(int i) const
{
    return Nodes::access_sudo(i);
}

const char* Maxscales::sshkey(int i) const
{
    return Nodes::sshkey(i);
}

const std::string& Maxscales::prefix() const
{
    return Nodes::prefix();
}

const char* Maxscales::ip4(int i) const
{
    return Nodes::ip4(i);
}

namespace maxtest
{

void MaxScale::wait_monitor_ticks(int ticks)
{
    for (int i = 0; i < ticks; i++)
    {
        auto res = curl_rest_api("maxscale/debug/monitor_wait");
        if (res.rc)
        {
            m_log.add_failure("Monitor wait failed. Error %i, %s", res.rc, res.output.c_str());
            break;
        }
    }
}

Nodes::SshResult MaxScale::curl_rest_api(const std::string& path)
{
    string cmd = mxb::string_printf("curl --silent --show-error http://%s:%s@%s:%s/v1/%s",
                                    m_rest_user.c_str(), m_rest_pw.c_str(),
                                    m_rest_ip.c_str(), m_rest_port.c_str(),
                                    path.c_str());
    auto res = m_maxscales->ssh_output(cmd, m_node_ind, true);
    return res;
}

MaxScale::MaxScale(Maxscales* maxscales, TestLogger& log, int node_ind)
    : m_maxscales(maxscales)
    , m_log(log)
    , m_node_ind(node_ind)
{
}

ServersInfo MaxScale::get_servers()
{
    const string field_servers = "servers";
    const string field_data = "data";
    const string field_id = "id";
    const string field_attr = "attributes";
    const string field_state = "state";
    const string field_mgroup = "master_group";
    const string field_rlag = "replication_lag";
    const string field_serverid = "server_id";
    const string field_slave_conns = "slave_connections";

    // Slave conn fields
    const string field_scon_name = "connection_name";
    const string field_scon_gtid = "gtid_io_pos";
    const string field_scon_id = "master_server_id";
    const string field_scon_io = "slave_io_running";
    const string field_scon_sql = "slave_sql_running";

    auto try_get_int = [](const Json& json, const string& key, int64_t failval) {
            int64_t rval = failval;
            json.try_get_int(key, &rval);
            return rval;
        };

    ServersInfo rval(m_log);
    auto res = curl_rest_api(field_servers);
    if (res.rc == 0)
    {
        Json all;
        if (all.load_string(res.output))
        {
            auto data = all.get_array_elems(field_data);
            for (auto& elem : data)
            {
                ServerInfo info;
                info.name = elem.get_string(field_id);
                auto attr = elem.get_object(field_attr);
                string state = attr.get_string(field_state);

                // The following depend on the monitor and may be null.
                info.master_group = try_get_int(attr, field_mgroup, ServerInfo::GROUP_NONE);
                info.rlag = try_get_int(attr, field_rlag, ServerInfo::RLAG_NONE);
                info.server_id = try_get_int(attr, field_serverid, ServerInfo::SRV_ID_NONE);
                info.status_from_string(state);

                if (attr.contains(field_slave_conns))
                {
                    auto conns = attr.get_array_elems(field_slave_conns);
                    info.slave_connections.reserve(conns.size());
                    for (auto& conn : conns)
                    {
                        using IO_State = ServerInfo::SlaveConnection::IO_State;
                        ServerInfo::SlaveConnection conn_info;
                        conn_info.name = conn.get_string(field_scon_name);
                        conn_info.gtid = conn.get_string(field_scon_gtid);
                        conn_info.master_id = conn.get_int(field_scon_id);
                        string io_running = conn.get_string(field_scon_io);
                        conn_info.io_running = (io_running == "Yes") ? IO_State::YES :
                            ((io_running == "Connecting") ? IO_State::CONNECTING : IO_State::NO);
                        string sql_running = conn.get_string(field_scon_sql);
                        conn_info.sql_running = (sql_running == "Yes");
                        info.slave_connections.push_back(std::move(conn_info));
                    }
                }
                rval.add(info);
            }
        }
        else
        {
            m_log.add_failure("Invalid data from REST-API servers query: %s", all.error_msg().c_str());
        }
    }
    else
    {
        m_log.add_failure("REST-API servers query failed. Error %i, %s", res.rc, mxb_strerror(res.rc));
    }
    return rval;
}

void ServersInfo::add(const ServerInfo& info)
{
    m_servers.push_back(info);
}

void ServersInfo::add(ServerInfo&& info)
{
    m_servers.push_back(std::move(info));
}

const ServerInfo& ServersInfo::get(size_t i) const
{
    return m_servers[i];
}

size_t ServersInfo::size() const
{
    return m_servers.size();
}

void ServersInfo::check_servers_status(const std::vector<ServerInfo::bitfield>& expected_status)
{
    // Checking only some of the servers is ok.
    auto n_expected = expected_status.size();
    if (n_expected <= m_servers.size())
    {
        for (size_t i = 0; i < n_expected; i++)
        {
            auto expected = expected_status[i];
            auto& info = m_servers[i];
            if (expected != info.status)
            {
                string found_str = info.status_to_string();
                string expected_str = ServerInfo::status_to_string(expected);
                m_log.add_failure("Wrong status for %s. Got '%s', expected '%s'.",
                                  info.name.c_str(), found_str.c_str(), expected_str.c_str());
            }
        }
    }
    else
    {
        m_log.add_failure("Expected at least %zu servers, found %zu.", n_expected, m_servers.size());
    }
}

void ServersInfo::check_master_groups(const std::vector<int>& expected_groups)
{
    auto n_expected = expected_groups.size();
    if (n_expected <= m_servers.size())
    {
        // Checking only some of the servers is ok.
        for (size_t i = 0; i < n_expected; i++)
        {
            auto expected = expected_groups[i];
            auto& info = m_servers[i];
            if (expected != info.master_group)
            {
                m_log.add_failure("Wrong master group for %s. Got '%li', expected '%i'.",
                                  info.name.c_str(), info.master_group, expected);
            }
        }
    }
    else
    {
        m_log.add_failure("Expected at least %zu servers, found %zu.", n_expected, m_servers.size());
    }
}

ServersInfo::ServersInfo(TestLogger& log)
    : m_log(log)
{
}

ServersInfo& ServersInfo::operator=(const ServersInfo& rhs)
{
    m_servers = rhs.m_servers;
    m_log = rhs.m_log;
    return *this;
}

ServersInfo::ServersInfo(ServersInfo&& rhs) noexcept
    : m_servers(std::move(rhs.m_servers))
    , m_log(rhs.m_log)
{
}

ServersInfo& ServersInfo::operator=(ServersInfo&& rhs) noexcept
{
    m_servers = std::move(rhs.m_servers);
    m_log = rhs.m_log;
    return *this;
}

void MaxScale::check_servers_status(const std::vector<ServerInfo::bitfield>& expected_status)
{
    auto data = get_servers();
    data.check_servers_status(expected_status);
}

void MaxScale::start()
{
    auto res = m_maxscales->start_maxscale(m_node_ind);
    m_log.expect(res == 0, "MaxScale start failed, error %i.", res);
}

void MaxScale::stop()
{
    auto res = m_maxscales->stop_maxscale(m_node_ind);
    m_log.expect(res == 0, "MaxScale stop failed, error %i.", res);
}

std::unique_ptr<mxt::MariaDB> MaxScale::open_rwsplit_connection(const std::string& db)
{
    auto conn = std::make_unique<mxt::MariaDB>(m_log);
    auto& sett = conn->connection_settings();
    sett.user = m_maxscales->user_name;
    sett.password = m_maxscales->password;
    if (m_maxscales->ssl)
    {
        sett.ssl.enabled = true;
        sett.ssl.key = mxb::string_printf("%s/ssl-cert/client-key.pem", test_dir);
        sett.ssl.cert = mxb::string_printf("%s/ssl-cert/client-cert.pem", test_dir);
        sett.ssl.ca = mxb::string_printf("%s/ssl-cert/ca.pem", test_dir);
    }

    conn->open(m_maxscales->ip(m_node_ind), m_maxscales->rwsplit_port[m_node_ind], db);
    return conn;
}

void MaxScale::alter_monitor(const string& mon_name, const string& setting, const string& value)
{
    string cmd = mxb::string_printf("alter monitor %s %s %s", mon_name.c_str(),
                                    setting.c_str(), value.c_str());
    auto res = m_maxscales->maxctrl(cmd);
    m_log.expect(res.rc == 0 && res.output == "OK", "Alter monitor command '%s' failed.", cmd.c_str());
}

void ServerInfo::status_from_string(const string& source)
{
    auto flags = mxb::strtok(source, ",");
    status = 0;
    for (string& flag : flags)
    {
        mxb::trim(flag);
        if (flag == "Running")
        {
            status |= RUNNING;
        }
        else if (flag == "Master")
        {
            status |= MASTER;
        }
        else if (flag == "Slave")
        {
            status |= SLAVE;
        }
        else if (flag == "Relay Master")
        {
            status |= RELAY;
        }
        else if (flag == "Slave of External Server")
        {
            status |= SERVER_SLAVE_OF_EXT_MASTER;
        }
        else if (flag == "Binlog Relay")
        {
            status |= BLR;
        }
    }
}

std::string ServerInfo::status_to_string(bitfield status)
{
    std::string rval;
    if (status)
    {
        std::vector<string> items;
        items.reserve(2);
        if (status & MASTER)
        {
            items.emplace_back("Master");
        }
        if (status & SLAVE)
        {
            items.emplace_back("Slave");
        }
        if (status & SERVER_SLAVE_OF_EXT_MASTER)
        {
            items.emplace_back("Slave of External Server");
        }
        if (status & BLR)
        {
            items.emplace_back("Binlog Relay");
        }
        if (status & RUNNING)
        {
            items.emplace_back("Running");
        }
        if (status & RELAY)
        {
            items.emplace_back("Relay Master");
        }
        rval = mxb::create_list_string(items);
    }
    return rval;
}

std::string ServerInfo::status_to_string() const
{
    return status_to_string(status);
}
}

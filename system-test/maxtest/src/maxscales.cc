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
#include <cassert>


using std::string;

namespace
{
const string my_prefix = "maxscale";
}

Maxscales::Maxscales(mxt::SharedData* shared)
    : Nodes(shared)
{
}

Maxscales::~Maxscales()
{
    for (int i = 0; i < N_MXS; ++i)
    {
        close_maxscale_connections(i);
    }
}

bool Maxscales::setup(const mxt::NetworkConfig& nwconfig, int n_min_expected)
{
    bool rval = false;
    int found = read_nodes_info(nwconfig);
    if (found < n_min_expected)
    {
        m_shared.log.add_failure("Found %i MaxScale(s) in network_config when at least %i was expected.",
                                 found, n_min_expected);
    }
    else
    {
        if (m_shared.settings.local_maxscale)
        {
            // MaxScale is running locally, overwrite node address.
            node(0).set_local();
        }

        if (Nodes::init_ssh_masters())
        {
            if (this->m_use_valgrind)
            {
                for (int i = 0; i < N; i++)
                {
                    ssh_node_f(i, true, "yum install -y valgrind gdb 2>&1");
                    ssh_node_f(i, true, "apt install -y --force-yes valgrind gdb 2>&1");
                    ssh_node_f(i, true, "zypper -n install valgrind gdb 2>&1");
                    ssh_node_f(i, true, "rm -rf /var/cache/maxscale/maxscale.lock");
                }
            }
            rval = true;
        }
    }
    return rval;
}

int Maxscales::read_nodes_info(const mxt::NetworkConfig& nwconfig)
{
    auto prefixc = my_prefix.c_str();
    string key_user = mxb::string_printf("%s_user", prefixc);
    user_name = envvar_get_set(key_user.c_str(), "skysql");

    string key_pw = mxb::string_printf("%s_password", prefixc);
    password = envvar_get_set(key_pw.c_str(), "skysql");

    clear_vms();
    int i = 0;
    while (i < N_MXS)
    {
        string node_name = mxb::string_printf("%s_%03d", prefixc, i);
        if (add_node(nwconfig, node_name))
        {
            string key_cnf = node_name + "_cnf";
            maxscale_cnf[i] = envvar_get_set(key_cnf.c_str(), "/etc/maxscale.cnf");

            string key_log_dir = node_name + "_log_dir";
            maxscale_log_dir[i] = envvar_get_set(key_log_dir.c_str(), "/var/log/maxscale/");

            string key_binlog_dir = node_name + "_binlog_dir";
            m_binlog_dir[i] = envvar_get_set(key_binlog_dir.c_str(), "/var/lib/maxscale/Binlog_Service/");

            rwsplit_port[i] = 4006;
            readconn_master_port[i] = 4008;
            readconn_slave_port[i] = 4009;

            ports[i][0] = rwsplit_port[i];
            ports[i][1] = readconn_master_port[i];
            ports[i][2] = readconn_slave_port[i];
            i++;
        }
        else
        {
            break;
        }
    }

    assert(i == Nodes::n_nodes());
    N = i;

    m_use_valgrind = readenv_bool("use_valgrind", false);
    m_use_callgrind = readenv_bool("use_callgrind", false);
    if (m_use_callgrind)
    {
        m_use_valgrind = true;
    }
    return i;
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
        if (verbose())
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
        if (verbose())
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
        if (verbose())
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
    if (m_use_valgrind)
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
    if (m_use_valgrind)
    {
        if (m_use_callgrind)
        {
            res = ssh_node_f(m, false,
                             "sudo --user=maxscale valgrind -d "
                             "--log-file=/%s/valgrind%02d.log --trace-children=yes "
                             " --tool=callgrind --callgrind-out-file=/%s/callgrind%02d.log "
                             " /usr/bin/maxscale",
                             maxscale_log_dir[m].c_str(), m_valgrind_log_num,
                             maxscale_log_dir[m].c_str(), m_valgrind_log_num);
        }
        else
        {
            res = ssh_node_f(m, false,
                             "sudo --user=maxscale valgrind --leak-check=full --show-leak-kinds=all "
                             "--log-file=/%s/valgrind%02d.log --trace-children=yes "
                             "--track-origins=yes /usr/bin/maxscale",
                             maxscale_log_dir[m].c_str(), m_valgrind_log_num);
        }
        m_valgrind_log_num++;
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
    if (m_use_valgrind)
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
    return my_prefix;
}

const char* Maxscales::ip4(int i) const
{
    return Nodes::ip4(i);
}

const std::string& Maxscales::node_name(int i) const
{
    return node(i).m_name;
}

mxt::CmdResult Maxscales::maxctrl(const string& cmd, int m, bool sudo)
{
    using CmdPriv = mxt::VMNode::CmdPriv;
    return node(m).run_cmd_output("maxctrl " + cmd, sudo ? CmdPriv::SUDO : CmdPriv::NORMAL);
}

bool Maxscales::use_valgrind() const
{
    return m_use_valgrind;
}

int Maxscales::restart(int m)
{
    return restart_maxscale(m);
}


int Maxscales::start(int m)
{
    return start_maxscale(m);
}

void Maxscales::stop_all()
{
    for (int i = 0; i < N; i++)
    {
        stop(i);
    }
}

int Maxscales::stop(int m)
{
    return stop_maxscale(m);
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
            logger().add_failure("Monitor wait failed. Error %i, %s", res.rc, res.output.c_str());
            break;
        }
    }
}

mxt::CmdResult MaxScale::curl_rest_api(const std::string& path)
{
    string cmd = mxb::string_printf("curl --silent --show-error http://%s:%s@%s:%s/v1/%s",
                                    m_rest_user.c_str(), m_rest_pw.c_str(),
                                    m_rest_ip.c_str(), m_rest_port.c_str(),
                                    path.c_str());
    auto res = m_maxscales->ssh_output(cmd, m_node_ind, true);
    return res;
}

MaxScale::MaxScale(Maxscales* maxscales, SharedData& shared, int node_ind)
    : m_maxscales(maxscales)
    , m_shared(shared)
    , m_node_ind(node_ind)
{
}

ServersInfo MaxScale::get_servers()
{
    using mxb::Json;
    const string field_servers = "servers";
    const string field_data = "data";
    const string field_id = "id";
    const string field_attr = "attributes";
    const string field_state = "state";
    const string field_mgroup = "master_group";
    const string field_rlag = "replication_lag";
    const string field_serverid = "server_id";
    const string field_slave_conns = "slave_connections";
    const string field_statistics = "statistics";

    // Slave conn fields
    const string field_scon_name = "connection_name";
    const string field_scon_gtid = "gtid_io_pos";
    const string field_scon_id = "master_server_id";
    const string field_scon_io = "slave_io_running";
    const string field_scon_sql = "slave_sql_running";

    // Statistics fields
    const string field_pers_conns = "persistent_connections";

    auto try_get_int = [](const Json& json, const string& key, int64_t failval) {
            int64_t rval = failval;
            json.try_get_int(key, &rval);
            return rval;
        };

    ServersInfo rval(&m_shared.log);
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

                auto stats = attr.get_object(field_statistics);
                info.pool_conns = try_get_int(stats, field_pers_conns, -1);

                rval.add(info);
            }
        }
        else
        {
            logger().add_failure("Invalid data from REST-API servers query: %s", all.error_msg().c_str());
        }
    }
    else
    {
        logger().add_failure("REST-API servers query failed. Error %i, %s", res.rc, mxb_strerror(res.rc));
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

void ServersInfo::check_servers_property(size_t n_expected, const std::function<void(size_t)>& tester)
{
    // Checking only some of the servers is ok.
    if (n_expected <= m_servers.size())
    {
        for (size_t i = 0; i < n_expected; i++)
        {
            tester(i);
        }
    }
    else
    {
        m_log->add_failure("Expected at least %zu servers, found %zu.", n_expected, m_servers.size());
    }
}

void ServersInfo::check_servers_status(const std::vector<ServerInfo::bitfield>& expected_status)
{
    auto tester = [&](size_t i) {
            auto expected = expected_status[i];
            auto& info = m_servers[i];
            if (expected != info.status)
            {
                string found_str = info.status_to_string();
                string expected_str = ServerInfo::status_to_string(expected);
                m_log->add_failure("Wrong status for %s. Got '%s', expected '%s'.",
                                   info.name.c_str(), found_str.c_str(), expected_str.c_str());
            }
        };
    check_servers_property(expected_status.size(), tester);
}

void ServersInfo::check_master_groups(const std::vector<int>& expected_groups)
{
    auto tester = [&](size_t i) {
            auto expected = expected_groups[i];
            auto& info = m_servers[i];
            if (expected != info.master_group)
            {
                m_log->add_failure("Wrong master group for %s. Got '%li', expected '%i'.",
                                   info.name.c_str(), info.master_group, expected);
            }
        };
    check_servers_property(expected_groups.size(), tester);
}

void ServersInfo::check_pool_connections(const std::vector<int>& expected_conns)
{
    auto tester = [&](size_t i) {
            auto expected = expected_conns[i];
            auto& info = m_servers[i];
            if (expected != info.pool_conns)
            {
                m_log->add_failure("Wrong connection pool size for %s. Got '%li', expected '%i'.",
                                   info.name.c_str(), info.pool_conns, expected);
            }
        };
    check_servers_property(expected_conns.size(), tester);
}

ServersInfo::ServersInfo(TestLogger* log)
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

ServerInfo ServersInfo::get_master() const
{
    ServerInfo rval;
    for (const auto& server : m_servers)
    {
        if (server.status & ServerInfo::MASTER)
        {
            rval = server;
            break;
        }
    }
    return rval;
}

void MaxScale::check_servers_status(const std::vector<ServerInfo::bitfield>& expected_status)
{
    auto data = get_servers();
    data.check_servers_status(expected_status);
}

void MaxScale::start()
{
    auto res = m_maxscales->start_maxscale(m_node_ind);
    logger().expect(res == 0, "MaxScale start failed, error %i.", res);
}

void MaxScale::stop()
{
    auto res = m_maxscales->stop_maxscale(m_node_ind);
    logger().expect(res == 0, "MaxScale stop failed, error %i.", res);
}

std::unique_ptr<mxt::MariaDB> MaxScale::open_rwsplit_connection(const std::string& db)
{
    auto conn = std::make_unique<mxt::MariaDB>(logger());
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
    logger().expect(res.rc == 0 && res.output == "OK", "Alter monitor command '%s' failed.", cmd.c_str());
}

TestLogger& MaxScale::logger()
{
    return m_shared.log;
}

mxt::CmdResult MaxScale::maxctrl(const string& cmd)
{
    return m_maxscales->maxctrl(cmd, m_node_ind);
}

const std::string& MaxScale::name() const
{
    return m_maxscales->node_name(m_node_ind);
}

void MaxScale::alter_service(const string& svc_name, const string& setting, const string& value)
{
    string cmd = mxb::string_printf("alter service %s %s %s", svc_name.c_str(),
                                    setting.c_str(), value.c_str());
    auto res = m_maxscales->maxctrl(cmd);
    logger().expect(res.rc == 0 && res.output == "OK", "Alter service command '%s' failed.", cmd.c_str());
}

void MaxScale::alter_server(const string& srv_name, const string& setting, const string& value)
{
    string cmd = mxb::string_printf("alter server %s %s %s", srv_name.c_str(),
                                    setting.c_str(), value.c_str());
    auto res = m_maxscales->maxctrl(cmd);
    logger().expect(res.rc == 0 && res.output == "OK", "Alter server command '%s' failed.", cmd.c_str());
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

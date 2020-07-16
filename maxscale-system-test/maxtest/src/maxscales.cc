#include <maxtest/maxscales.hh>
#include <string>
#include <maxbase/string.hh>
#include <maxtest/envv.hh>
#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>
#include <maxbase/jansson.h>
#include <maxtest/json.hh>

#define DEFAULT_MAXSCALE_CNF "/etc/maxscale.cnf"
#define DEFAULT_MAXSCALE_LOG_DIR "/var/log/maxscale/"
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

    if ((N > 0) && (N < 255))
    {
        for (int i = 0; i < N; i++)
        {
            sprintf(env_name, "%s_%03d_cnf", prefix, i);
            maxscale_cnf[i] = readenv(env_name, DEFAULT_MAXSCALE_CNF);

            sprintf(env_name, "%s_%03d_log_dir", prefix, i);
            maxscale_log_dir[i] = readenv(env_name, DEFAULT_MAXSCALE_LOG_DIR);

            sprintf(env_name, "%s_%03d_binlog_dir", prefix, i);
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
    if (use_ipv6)
    {
        conn_rwsplit[m] = open_conn_db(rwsplit_port[m],
                                       IP6[m],
                                       db,
                                       user_name,
                                       password,
                                       ssl);
    }
    else
    {
        conn_rwsplit[m] = open_conn_db(rwsplit_port[m],
                                       IP[m],
                                       db,
                                       user_name,
                                       password,
                                       ssl);
    }
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
    if (use_ipv6)
    {
        conn_master[m] = open_conn_db(readconn_master_port[m],
                                      IP6[m],
                                      db,
                                      user_name,
                                      password,
                                      ssl);
    }
    else
    {
        conn_master[m] = open_conn_db(readconn_master_port[m],
                                      IP[m],
                                      db,
                                      user_name,
                                      password,
                                      ssl);
    }
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
    if (use_ipv6)
    {
        conn_slave[m] = open_conn_db(readconn_slave_port[m],
                                     IP6[m],
                                     db,
                                     user_name,
                                     password,
                                     ssl);
    }
    else
    {
        conn_slave[m] = open_conn_db(readconn_slave_port[m],
                                     IP[m],
                                     db,
                                     user_name,
                                     password,
                                     ssl);
    }
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
                             maxscale_log_dir[m], valgring_log_num,
                             maxscale_log_dir[m], valgring_log_num);
        }
        else
        {
            res = ssh_node_f(m, false,
                             "sudo --user=maxscale valgrind --leak-check=full --show-leak-kinds=all "
                             "--log-file=/%s/valgrind%02d.log --trace-children=yes "
                             "--track-origins=yes /usr/bin/maxscale", maxscale_log_dir[m], valgring_log_num);
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

void MaxScale::wait_monitor_ticks(int ticks)
{
    for (int i = 0; i < ticks; i++)
    {
        auto res = curl_rest_api("maxscale/debug/monitor_wait");
        if (res.rc)
        {
            m_tester.expect(false, "Monitor wait failed. Error %i, %s", res.rc, res.output.c_str());
            break;
        }
    }
}

Nodes::SshResult MaxScale::curl_rest_api(const std::string& path)
{
    string cmd = mxb::string_printf("curl --silent --show-error http://%s:%s@%s:%s/v1/%s",
        m_rest_user.c_str(), m_rest_pw.c_str(), m_rest_ip.c_str(), m_rest_port.c_str(), path.c_str());
    auto res = m_tester.maxscales->ssh_output(cmd, m_node_ind, true);
    return res;
}

MaxScale::MaxScale(TestConnections& tester, int node_ind)
    : m_tester(tester)
    , m_node_ind(node_ind)
{
}

ServersInfo MaxScale::get_servers()
{
    ServersInfo rval;
    auto res = curl_rest_api("servers");
    if (res.rc == 0)
    {
        Json all;
        if (all.load_string(res.output))
        {
            auto data = all.get_array_elems("data");
            for (auto& elem : data)
            {
                ServerInfo info;
                info.name = elem.get_string("id");
                auto attr = elem.get_object("attributes");
                string state = attr.get_string("state");
                info.status_from_string(state);
                rval.add(info);
            }
        }
        else
        {
            m_tester.add_failure("Invalid data from REST-API servers query: %s", all.error_msg().c_str());
        }
    }
    else
    {
        m_tester.add_failure("REST-API servers query failed. Error %i, %s", res.rc, mxb_strerror(res.rc));
    }
    return rval;
}

void ServersInfo::add(const ServerInfo& info)
{
     m_servers.push_back(info);
}

const ServerInfo& ServersInfo::get(size_t i)
{
    return m_servers[i];
}

size_t ServersInfo::size() const
{
    return m_servers.size();
}

void MaxScale::check_servers_status(std::vector<uint> expected_status)
{
    auto data = get_servers();

    // Checking only some of the servers is ok.
    auto n_expected = expected_status.size();
    if (n_expected <= data.size())
    {
        for (size_t i = 0; i < n_expected; i++)
        {
            uint expected = expected_status[i];
            auto& info = data.get(i);
            uint found = info.status;
            if (expected != found)
            {
                string found_str = info.status_to_string();
                string expected_str = ServerInfo::status_to_string(expected);
                m_tester.add_failure("Wrong status for %s. Got '%s', expected '%s'.",
                                     info.name.c_str(), found_str.c_str(), expected_str.c_str());
            }
        }
    }
    else
    {
        m_tester.add_failure("Expected at least %zu servers, found %zu.", n_expected, data.size());
    }
}

void ServerInfo::status_from_string(const string& source)
{
    auto flags = mxb::strtok(source, ",");
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
    }
}

std::string ServerInfo::status_to_string(uint status)
{
    std::string rval;
    if (status)
    {
        std::vector<string> items;
        if (status & MASTER)
        {
            items.push_back("Master");
        }
        if (status & SLAVE)
        {
            items.push_back("Slave");
        }
        if (status & RUNNING)
        {
            items.push_back("Running");
        }
        if (status & RELAY)
        {
            items.push_back("Relay Master");
        }
        rval = mxb::create_list_string(items);
    }
    return rval;
}

std::string ServerInfo::status_to_string() const
{
    return status_to_string(status);
}

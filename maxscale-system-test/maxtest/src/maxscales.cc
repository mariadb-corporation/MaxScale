#include <maxtest/maxscales.hh>
#include <string>
#include <maxbase/string.hh>
#include <maxtest/envv.hh>

#define DEFAULT_MAXSCALE_CNF "/etc/maxscale.cnf"
#define DEFAULT_MAXSCALE_LOG_DIR "/var/log/maxscale/"
#define DEFAULT_MAXSCALE_BINLOG_DIR "/var/lib/maxscale/Binlog_Service/"

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

#include "maxscales.h"
#include <sstream>
#include <unordered_map>
#include <string>
#include "envv.h"

Maxscales::Maxscales(const char *pref, const char *test_cwd, bool verbose,
                     std::string network_config)
{
    strcpy(prefix, pref);
    this->verbose = verbose;
    valgring_log_num = 0;
    strcpy(test_dir, test_cwd);
    this->network_config = network_config;
    read_env();
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

            sprintf(env_name, "%s_%03d_maxadmin_password", prefix, i);
            maxadmin_password[i] = readenv(env_name, DEFAULT_MAXADMIN_PASSWORD);

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
            printf("Failed to connect to readwritesplit: %d, %s", my_errno, mysql_error(conn_rwsplit[m]));
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
            printf("Failed to connect to readwritesplit: %d, %s", my_errno, mysql_error(conn_master[m]));
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
            printf("Failed to connect to readwritesplit: %d, %s", my_errno, mysql_error(conn_slave[m]));
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
        res =ssh_node(m, "service maxscale restart", true);
    }
    fflush(stdout);
    return res;
}

int Maxscales::stop_maxscale(int m)
{
    int res;
    if (use_valgrind)
    {
        res = ssh_node_f(m, true, "sudo kill $(pidof valgrind) 2>&1 > /dev/null");
        if ((res != 0) || atoi(ssh_node_output(m, "pidof valgrind", true, &res)) > 0)
        {
            res = ssh_node_f(m, true, "sudo kill -9 $(pidof valgrind) 2>&1 > /dev/null");
        }
    }
    else
    {
        res = ssh_node(m, "service maxscale stop", true);
    }
    fflush(stdout);
    return res;
}

int Maxscales::execute_maxadmin_command(int m, const char* cmd)
{
    return ssh_node_f(m, true, "maxadmin %s", cmd);
}
int Maxscales::execute_maxadmin_command_print(int m, const char* cmd)
{
    int exit_code;
    printf("%s\n", ssh_node_output_f(m, true, &exit_code, "maxadmin %s", cmd));
    return exit_code;
}

int Maxscales::check_maxadmin_param(int m, const char* command, const char* param, const char* value)
{
    char result[1024];
    int rval = 1;

    if (get_maxadmin_param(m, (char*)command, (char*)param, (char*)result) == 0)
    {
        char* end = strchr(result, '\0') - 1;

        while (isspace(*end))
        {
            *end-- = '\0';
        }

        char* start = result;

        while (isspace(*start))
        {
            start++;
        }

        if (strcmp(start, value) == 0)
        {
            rval = 0;
        }
        else
        {
            printf("Expected %s, got %s\n", value, start);
        }
    }

    return rval;
}

int Maxscales::get_maxadmin_param(int m, const char* command, const char* param, char* result)
{
    char* buf;
    int exit_code;

    buf = ssh_node_output_f(m, true, &exit_code, "maxadmin %s", command);

    // printf("%s\n", buf);

    char* x = strstr(buf, param);

    if (x == NULL)
    {
        return 1;
    }

    x += strlen(param);

    // Skip any trailing parts of the parameter name
    while (!isspace(*x))
    {
        x++;
    }

    // Trim leading whitespace
    while (!isspace(*x))
    {
        x++;
    }

    char* end = strchr(x, '\n');

    // Trim trailing whitespace
    while (isspace(*end))
    {
        *end-- = '\0';
    }

    strcpy(result, x);

    return exit_code;
}

int Maxscales::get_backend_servers_num(int m, const char* service)
{
    char* buf;
    int exit_code;
    int i = 0;

    buf = ssh_node_output_f(m, true, &exit_code, "maxadmin show service %s | grep Name: | grep Protocol: | wc -l", service);
    if (buf && !exit_code)
    {
        sscanf(buf, "%d", &i);
    }
    return i;
}



long unsigned Maxscales::get_maxscale_memsize(int m)
{
    int exit_code;
    char* ps_out = ssh_node_output(m, "ps -e -o pid,vsz,comm= | grep maxscale", false, &exit_code);
    long unsigned mem = 0;
    pid_t pid;
    sscanf(ps_out, "%d %lu", &pid, &mem);
    return mem;
}


int Maxscales::find_master_maxadmin(Mariadb_nodes* nodes, int m)
{
    bool found = false;
    int master = -1;

    for (int i = 0; i < nodes->N; i++)
    {
        char show_server[256];
        char res[256];
        sprintf(show_server, "show server server%d", i + 1);
        get_maxadmin_param(m, show_server, (char*) "Status", res);

        if (strstr(res, "Master"))
        {
            if (found)
            {
                master = -1;
            }
            else
            {
                master = i;
                found = true;
            }
        }
    }

    return master;
}

int Maxscales::find_slave_maxadmin(Mariadb_nodes* nodes, int m)
{
    int slave = -1;

    for (int i = 0; i < nodes->N; i++)
    {
        char show_server[256];
        char res[256];
        sprintf(show_server, "show server server%d", i + 1);
        get_maxadmin_param(m, show_server, (char*) "Status", res);

        if (strstr(res, "Slave"))
        {
            slave = i;
        }
    }

    return slave;
}

StringSet Maxscales::get_server_status(const char* name, int m)
{
    std::set<std::string> rval;
    int exit_code;
    char* res = ssh_node_output_f(m, true, &exit_code, "maxadmin list servers|grep \'%s\'", name);
    char* pipe = strrchr(res, '|');

    if (res && pipe)
    {
        pipe++;
        char* tok = strtok(pipe, ",");

        while (tok)
        {
            char* p = tok;
            char* end = strchr(tok, '\n');
            if (!end)
            {
                end = strchr(tok, '\0');
            }

            // Trim leading whitespace
            while (p < end && isspace(*p))
            {
                p++;
            }

            // Trim trailing whitespace
            while (end > tok && isspace(*end))
            {
                *end-- = '\0';
            }

            rval.insert(p);
            tok = strtok(NULL, ",\n");
        }

        free(res);
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
    ssh_node_f(m, false, "for ((i=0;i<%d;i++)); do maxctrl api get maxscale/debug/monitor_wait; done", intervals);
}

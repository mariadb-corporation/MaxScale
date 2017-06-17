#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

#include "mariadb_func.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"
#include "testconnections.h"

namespace maxscale
{
static bool start = true;
static bool check_nodes = true;
static std::string required_repl_version;
static std::string required_galera_version;
}

void TestConnections::check_nodes(bool value)
{
    maxscale::check_nodes = value;
}

void TestConnections::skip_maxscale_start(bool value)
{
    maxscale::start = !value;
}

void TestConnections::require_repl_version(const char *version)
{
    maxscale::required_repl_version = version;
}

void TestConnections::require_galera_version(const char *version)
{
    maxscale::required_galera_version = version;
}

TestConnections::TestConnections(int argc, char *argv[]):
    no_backend_log_copy(false), use_snapshots(false), verbose(false), rwsplit_port(4006),
    readconn_master_port(4008), readconn_slave_port(4009), binlog_port(5306),
    global_result(0), binlog_cmd_option(0), enable_timeouts(true), use_ipv6(false),
    no_galera(false)
{
    chdir(test_dir);
    gettimeofday(&start_time, NULL);
    ports[0] = rwsplit_port;
    ports[1] = readconn_master_port;
    ports[2] = readconn_slave_port;

    read_env();

    char * gal_env = getenv("galera_000_network");
    if ((gal_env == NULL) || (strcmp(gal_env, "") == 0 ))
    {
        no_galera = true;
        tprintf("Galera backend variables are not defined, Galera won't be used\n");
    }

    bool maxscale_init = true;

    static struct option long_options[] =
    {

        {"verbose", no_argument, 0, 'v'},
        {"silent", no_argument, 0, 'n'},
        {"help", no_argument, 0, 'h'},
        {"no-maxscale-start", no_argument, 0, 's'},
        {"no-nodes-check", no_argument, 0, 'r'},
        {"quiet", no_argument, 0, 'q'},
        {"restart-galera", no_argument, 0, 'g'},
        {"no-timeouts", no_argument, 0, 'z'},
        {"no-galera", no_argument, 0, 'y'},
        {0, 0, 0, 0}
    };

    int c;
    int option_index = 0;
    bool restart_galera = false;

    while ((c = getopt_long(argc, argv, "vnqhsirgzy", long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case 'v':
            verbose = true;
            break;

        case 'n':
            verbose = false;
            break;

        case 'q':
            freopen("/dev/null", "w", stdout);
            break;

        case 'h':
            printf("Options:\n"
                   "-h, --help\n"
                   "-v, --verbose\n"
                   "-q, --silent\n"
                   "-s, --no-maxscale-start\n"
                   "-i, --no-maxscale-init\n"
                   "-g, --restart-galera\n"
                   "-y, --no-galera\n"
                   "-z, --no-timeouts\n");
            exit(0);
            break;

        case 's':
            printf("Maxscale won't be started\n");
            maxscale::start = false;
            break;
        case 'i':
            printf("Maxscale won't be started and Maxscale.cnf won't be uploaded\n");
            maxscale_init = false;
            break;

        case 'r':
            printf("Nodes are not checked before test and are not restarted\n");
            maxscale::check_nodes = false;
            break;

        case 'g':
            printf("Restarting Galera setup\n");
            restart_galera = true;
            break;

        case 'z':
            enable_timeouts = false;
            break;

        case 'y':
            printf("Do not use Galera setup\n");
            no_galera = true;
            break;

        default:
            printf("UNKNOWN OPTION: %c\n", c);
            break;
        }
    }

    if (optind < argc)
    {
        test_name = argv[optind];
    }
    else
    {
        test_name = basename(argv[0]);
    }

    sprintf(get_logs_command, "%s/get_logs.sh", test_dir);

    sprintf(ssl_options, "--ssl-cert=%s/ssl-cert/client-cert.pem --ssl-key=%s/ssl-cert/client-key.pem",
            test_dir, test_dir);
    setenv("ssl_options", ssl_options, 1);

    repl = new Mariadb_nodes("node", test_dir, verbose);
    if (!no_galera)
    {
        galera = new Galera_nodes("galera", test_dir, verbose);
        //galera->use_ipv6 = use_ipv6;
        galera->use_ipv6 = false;
    }
    else
    {
        galera = repl;
    }

    repl->use_ipv6 = use_ipv6;


    if (maxscale::required_repl_version.length())
    {
        int ver_repl_required = get_int_version(maxscale::required_repl_version);
        std::string ver_repl = repl->get_lowest_version();
        int int_ver_repl = get_int_version(ver_repl);

        if (int_ver_repl < ver_repl_required)
        {
            tprintf("Test requires a higher version of backend servers, skipping test.");
            tprintf("Required version: %s", maxscale::required_repl_version.c_str());
            tprintf("Master-slave version: %s", ver_repl.c_str());
            exit(0);
        }
    }

    if (maxscale::required_galera_version.length())
    {
        int ver_galera_required = get_int_version(maxscale::required_galera_version);
        std::string ver_galera = galera->get_lowest_version();
        int int_ver_galera = get_int_version(ver_galera);

        if (int_ver_galera < ver_galera_required)
        {
            tprintf("Test requires a higher version of backend servers, skipping test.");
            tprintf("Required version: %s", maxscale::required_galera_version.c_str());
            tprintf("Galera version: %s", ver_galera.c_str());
            exit(0);
        }
    }

    if ((restart_galera) && (!no_galera))
    {
        galera->stop_nodes();
        galera->start_replication();
    }

    bool snapshot_reverted = false;

    if (use_snapshots)
    {
        snapshot_reverted = revert_snapshot((char *) "clean");
    }

    if (!snapshot_reverted && maxscale::check_nodes)
    {
        if (!repl->fix_replication() )
        {
            exit(200);
        }
        if (!no_galera)
        {
            if (!galera->fix_replication())
            {
                exit(200);
            }
        }
    }

    if (maxscale_init)
    {
        init_maxscale();
    }

    if (backend_ssl)
    {
        tprintf("Configuring backends for ssl \n");
        repl->configure_ssl(true);
        if (!no_galera)
        {
            galera->configure_ssl(false);
            galera->start_replication();
        }
    }

    char str[1024];
    sprintf(str, "mkdir -p LOGS/%s", test_name);
    system(str);

    timeout = 999999999;
    set_log_copy_interval(999999999);
    pthread_create( &timeout_thread_p, NULL, timeout_thread, this);
    pthread_create( &log_copy_thread_p, NULL, log_copy_thread, this);
    tprintf("Starting test");
    gettimeofday(&start_time, NULL);
}

TestConnections::~TestConnections()
{
    if (backend_ssl)
    {
        repl->disable_ssl();
        //galera->disable_ssl();
    }

    copy_all_logs();

    if (global_result != 0 )
    {
        tprintf("Reverting snapshot\n");
        revert_snapshot((char*) "clean");
    }

    delete repl;
    if (!no_galera)
    {
        delete galera;
    }
}

void TestConnections::add_result(int result, const char *format, ...)
{
    timeval t2;
    gettimeofday(&t2, NULL);
    double elapsedTime = (t2.tv_sec - start_time.tv_sec);
    elapsedTime += (double) (t2.tv_usec - start_time.tv_usec) / 1000000.0;

    if (result != 0)
    {
        global_result += result;

        printf("%04f: TEST_FAILED! ", elapsedTime);

        va_list argp;
        va_start(argp, format);
        vprintf(format, argp);
        va_end(argp);

        if (format[strlen(format) - 1] != '\n')
        {
            printf("\n");
        }
    }
}

int TestConnections::read_env()
{

    char *env;

    if (verbose)
    {
        printf("Reading test setup configuration from environmental variables\n");
    }

    env = getenv("maxscale_IP");
    if (env != NULL)
    {
        sprintf(maxscale_IP, "%s", env);
    }
    env = getenv("maxscale_network6");
    if (env != NULL)
    {
        sprintf(maxscale_IP6, "%s", env);
    }
    env = getenv("maxscale_user");
    if (env != NULL)
    {
        sprintf(maxscale_user, "%s", env);
    }
    else
    {
        sprintf(maxscale_user, "skysql");
    }
    env = getenv("maxscale_password");
    if (env != NULL)
    {
        sprintf(maxscale_password, "%s", env);
    }
    else
    {
        sprintf(maxscale_password, "skysql");
    }
    env = getenv("maxadmin_password");
    if (env != NULL)
    {
        sprintf(maxadmin_password, "%s", env);
    }
    else
    {
        sprintf(maxadmin_password, "mariadb");
    }
    env = getenv("maxscale_keyfile");
    if (env != NULL)
    {
        sprintf(maxscale_keyfile, "%s", env);
    }
    else
    {
        sprintf(maxscale_keyfile, "skysql");
    }

    //env = getenv("get_logs_command"); if (env != NULL) {sprintf(get_logs_command, "%s", env);}

    env = getenv("sysbench_dir");
    if (env != NULL)
    {
        sprintf(sysbench_dir, "%s", env);
    }

    env = getenv("maxscale_cnf");
    if (env != NULL)
    {
        sprintf(maxscale_cnf, "%s", env);
    }
    else
    {
        sprintf(maxscale_cnf, "/etc/maxscale.cnf");
    }
    env = getenv("maxscale_log_dir");
    if (env != NULL)
    {
        sprintf(maxscale_log_dir, "%s", env);
    }
    else
    {
        sprintf(maxscale_log_dir, "/var/log/maxscale/");
    }
    env = getenv("maxscale_binlog_dir");
    if (env != NULL)
    {
        sprintf(maxscale_binlog_dir, "%s", env);
    }
    else
    {
        sprintf(maxscale_binlog_dir, "/var/lib/maxscale/Binlog_Service/");
    }
    //env = getenv("test_dir"); if (env != NULL) {sprintf(test_dir, "%s", env);}
    env = getenv("maxscale_whoami");
    if (env != NULL)
    {
        sprintf(maxscale_access_user, "%s", env);
    }
    env = getenv("maxscale_access_sudo");
    if (env != NULL)
    {
        sprintf(maxscale_access_sudo, "%s", env);
    }
    ssl = false;
    env = getenv("ssl");
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) ))
    {
        ssl = true;
    }
    env = getenv("mysql51_only");
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) ))
    {
        maxscale::check_nodes = false;
    }

    env = getenv("no_nodes_check");
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) ))
    {
        maxscale::check_nodes = false;
    }
    env = getenv("no_backend_log_copy");
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) ))
    {
        no_backend_log_copy = true;
    }
    env = getenv("use_ipv6");
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) ))
    {
        use_ipv6 = true;
    }

    env = getenv("maxscale_hostname");
    if (env != NULL)
    {
        sprintf(maxscale_hostname, "%s", env);
    }
    else
    {
        sprintf(maxscale_hostname, "%s", maxscale_IP);
    }

    env = getenv("backend_ssl");
    if (env != NULL && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) ))
    {
        backend_ssl = true;
    }
    else
    {
        backend_ssl = false;
    }

    if (strcmp(maxscale_access_user, "root") == 0)
    {
        sprintf(maxscale_access_homedir, "/%s/", maxscale_access_user);
    }
    else
    {
        sprintf(maxscale_access_homedir, "/home/%s/", maxscale_access_user);
    }

    env = getenv("smoke");
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) ))
    {
        smoke = true;
    }
    else
    {
        smoke = false;
    }
    env = getenv("threads");
    if ((env != NULL))
    {
        sscanf(env, "%d", &threads);
    }
    else
    {
        threads = 4;
    }

    env = getenv("use_snapshots");
    if (env != NULL && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) ))
    {
        use_snapshots = true;
    }
    else
    {
        use_snapshots = false;
    }
    env = getenv("take_snapshot_command");
    if (env != NULL)
    {
        sprintf(take_snapshot_command, "%s", env);
    }
    else
    {
        sprintf(take_snapshot_command, "exit 1");
    }
    env = getenv("revert_snapshot_command");
    if (env != NULL)
    {
        sprintf(revert_snapshot_command, "%s", env);
    }
    else
    {
        sprintf(revert_snapshot_command, "exit 1");
    }

    env = getenv("no_maxscale_start");
    if (env != NULL && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) ))
    {
        maxscale::start = false;
    }
}

int TestConnections::print_env()
{
    int  i;
    printf("Maxscale IP\t%s\n", maxscale_IP);
    printf("Maxscale User name\t%s\n", maxscale_user);
    printf("Maxscale Password\t%s\n", maxscale_password);
    printf("Maxscale SSH key\t%s\n", maxscale_keyfile);
    printf("Maxadmin password\t%s\n", maxadmin_password);
    printf("Access user\t%s\n", maxscale_access_user);
    repl->print_env();
    galera->print_env();
}

const char * get_template_name(char * test_name)
{
    int i = 0;
    while (cnf_templates[i].test_name && strcmp(cnf_templates[i].test_name, test_name) != 0)
    {
        i++;
    }

    if (cnf_templates[i].test_name)
    {
        return cnf_templates[i].test_template;
    }

    printf("Failed to find configuration template for test '%s', using default template '%s'.\n", test_name,
           default_template);
    return default_template;
}

void TestConnections::process_template(const char *template_name, const char *dest)
{
    char str[4096];
    char template_file[1024];

    sprintf(template_file, "%s/cnf/maxscale.cnf.template.%s", test_dir, template_name);
    sprintf(str, "cp %s maxscale.cnf", template_file);
    if (system(str) != 0)
    {
        tprintf("Error copying maxscale.cnf template\n");
        return;
    }

    if (backend_ssl)
    {
        tprintf("Adding ssl settings\n");
        system("sed -i \"s|type=server|type=server\\nssl=required\\nssl_cert=/###access_homedir###/certs/client-cert.pem\\nssl_key=/###access_homedir###/certs/client-key.pem\\nssl_ca_cert=/###access_homedir###/certs/ca.pem|g\" maxscale.cnf");
    }

    sprintf(str, "sed -i \"s/###threads###/%d/\"  maxscale.cnf", threads);
    system(str);

    Mariadb_nodes * mdn[2];
    char * IPcnf;
    mdn[0] = repl;
    mdn[1] = galera;
    int i, j;

    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < mdn[j]->N; i++)
        {
            if (mdn[j]->use_ipv6)
            {
                IPcnf = mdn[j]->IP6[i];
            }
            else
            {
                IPcnf = mdn[j]->IP[i];
            }
            sprintf(str, "sed -i \"s/###%s_server_IP_%0d###/%s/\" maxscale.cnf",
                    mdn[j]->prefix, i + 1, IPcnf);
            system(str);

            sprintf(str, "sed -i \"s/###%s_server_port_%0d###/%d/\" maxscale.cnf",
                    mdn[j]->prefix, i + 1, mdn[j]->port[i]);
            system(str);
        }

        mdn[j]->connect();
        execute_query(mdn[j]->nodes[0], (char *) "CREATE DATABASE IF NOT EXISTS test");
        mdn[j]->close_connections();
    }

    sprintf(str, "sed -i \"s/###access_user###/%s/g\" maxscale.cnf", maxscale_access_user);
    system(str);

    sprintf(str, "sed -i \"s|###access_homedir###|%s|g\" maxscale.cnf", maxscale_access_homedir);
    system(str);

    if (repl->v51)
    {
        system("sed -i \"s/###repl51###/mysql51_replication=true/g\" maxscale.cnf");
    }
    copy_to_maxscale((char *) "maxscale.cnf", (char *) dest);
}

int TestConnections::init_maxscale()
{
    const char * template_name = get_template_name(test_name);
    tprintf("Template is %s\n", template_name);

    process_template(template_name, maxscale_access_homedir);

    ssh_maxscale(true, "cp maxscale.cnf %s;rm -rf %s/certs;mkdir -m a+wrx %s/certs;", maxscale_cnf,
                 maxscale_access_homedir, maxscale_access_homedir);

    char str[4096];
    char dtr[4096];
    sprintf(str, "%s/ssl-cert/*", test_dir);
    sprintf(dtr, "%s/certs/", maxscale_access_homedir);
    copy_to_maxscale(str, dtr);
    sprintf(str, "cp %s/ssl-cert/* .", test_dir);
    system(str);

    ssh_maxscale(true, "chown maxscale:maxscale -R %s/certs;"
                 "chmod 664 %s/certs/*.pem;"
                 " chmod a+x %s;"
                 "%s"
                 "iptables -I INPUT -p tcp --dport 4001 -j ACCEPT;"
                 "rm -f %s/maxscale.log %s/maxscale1.log;"
                 "rm -rf /tmp/core* /dev/shm/* /var/lib/maxscale/maxscale.cnf.d/ /var/lib/maxscale/*;"
                 "%s",
                 maxscale_access_homedir, maxscale_access_homedir, maxscale_access_homedir,
                 maxscale::start ? "killall -9 maxscale;" : "",
                 maxscale_log_dir, maxscale_log_dir, maxscale::start ? "service maxscale restart" : "");

    fflush(stdout);

    if (maxscale::start)
    {
        int waits;

        for (waits = 0; waits < 15; waits++)
        {
            if (ssh_maxscale(true, "/bin/sh -c \"maxadmin help > /dev/null || exit 1\"") == 0)
            {
                break;
            }
            sleep(1);
        }

        if (waits > 0)
        {
            tprintf("Waited %d seconds for MaxScale to start", waits);
        }
    }
}

int TestConnections::connect_maxscale()
{
    return connect_rwsplit() +
           connect_readconn_master() +
           connect_readconn_slave();
}

int TestConnections::close_maxscale_connections()
{
    mysql_close(conn_master);
    mysql_close(conn_slave);
    mysql_close(conn_rwsplit);
}

int TestConnections::restart_maxscale()
{
    sleep(15);
    int res = ssh_maxscale(true, "service maxscale restart");
    fflush(stdout);
    sleep(10);
    return res;
}

int TestConnections::start_maxscale()
{
    sleep(15);
    int res = ssh_maxscale(true, "service maxscale start");
    fflush(stdout);
    sleep(10);
    return res;
}

int TestConnections::stop_maxscale()
{
    int res = ssh_maxscale(true, "service maxscale stop");
    check_maxscale_processes(0);
    fflush(stdout);
    return res;
}

int TestConnections::copy_mariadb_logs(Mariadb_nodes * repl, char * prefix)
{
    int local_result = 0;
    char * mariadb_log;
    FILE * f;
    int i;
    int exit_code;
    char str[4096];

    sprintf(str, "mkdir -p LOGS/%s", test_name);
    system(str);
    for (i = 0; i < repl->N; i++)
    {
        if (strcmp(repl->IP[i], "127.0.0.1") != 0) // Do not copy MariaDB logs in case of local backend
        {
            mariadb_log = repl->ssh_node_output(i, (char *) "cat /var/lib/mysql/*.err", true, &exit_code);
            sprintf(str, "LOGS/%s/%s%d_mariadb_log", test_name, prefix, i);
            f = fopen(str, "w");
            if (f != NULL)
            {
                fwrite(mariadb_log, sizeof(char), strlen(mariadb_log), f);
                fclose(f);
            }
            else
            {
                printf("Error writing MariaDB log");
                local_result = 1;
            }
            free(mariadb_log);
        }
    }
    return local_result;
}

int TestConnections::copy_all_logs()
{
    char str[4096];
    set_timeout(300);

    if (!no_backend_log_copy)
    {
        copy_mariadb_logs(repl, (char *) "node");
        copy_mariadb_logs(galera, (char *) "galera");
    }

    sprintf(str, "%s/copy_logs.sh %s", test_dir, test_name);
    tprintf("Executing %s\n", str);
    if (system(str) != 0)
    {
        tprintf("copy_logs.sh executing FAILED!\n");
        return 1;
    }
    else
    {
        tprintf("copy_logs.sh OK!\n");
        return 0;
    }
}

int TestConnections::copy_all_logs_periodic()
{
    char str[4096];
    //set_timeout(300);

    timeval t2;
    gettimeofday(&t2, NULL);
    double elapsedTime = (t2.tv_sec - start_time.tv_sec);
    elapsedTime += (double) (t2.tv_usec - start_time.tv_usec) / 1000000.0;

    sprintf(str, "%s/copy_logs.sh %s %04f", test_dir, test_name, elapsedTime);
    tprintf("Executing %s\n", str);
    if (system(str) != 0)
    {
        tprintf("copy_logs.sh executing FAILED!\n");
        return 1;
    }
    else
    {
        tprintf("copy_logs.sh OK!\n");
        return 0;
    }
}

int TestConnections::prepare_binlog()
{
    char version_str[1024];
    find_field(repl->nodes[0], "SELECT @@VERSION", "@@version", version_str);
    tprintf("Master server version %s\n", version_str);

    if ((strstr(version_str, "10.0") != NULL) ||
            (strstr(version_str, "10.1") != NULL) ||
            (strstr(version_str, "10.2") != NULL))
    {
        tprintf("10.0!\n");
    }
    else
    {
        add_result(ssh_maxscale(true,
                                "sed -i \"s/,mariadb10-compatibility=1//\" %s",
                                maxscale_cnf), "Error editing maxscale.cnf");
    }

    tprintf("Removing all binlog data from Maxscale node\n");
    add_result(ssh_maxscale(true, "rm -rf %s", maxscale_binlog_dir),
               "Removing binlog data failed\n");

    tprintf("Creating binlog dir\n");
    add_result(ssh_maxscale(true, "mkdir -p %s", maxscale_binlog_dir),
               "Creating binlog data dir failed\n");
    tprintf("Set 'maxscale' as a owner of binlog dir\n");
    add_result(ssh_maxscale(false,
                            "%s mkdir -p %s; %s chown maxscale:maxscale -R %s",
                            maxscale_access_sudo, maxscale_binlog_dir,
                            maxscale_access_sudo, maxscale_binlog_dir),
               "directory ownership change failed\n");
    return 0;
}

int TestConnections::start_binlog()
{
    char sys1[4096];
    MYSQL * binlog;
    char log_file[256];
    char log_pos[256];
    char cmd_opt[256];

    int i;
    int global_result = 0;
    bool no_pos;

    no_pos = repl->no_set_pos;

    switch (binlog_cmd_option)
    {
    case 1:
        sprintf(cmd_opt, "--binlog-checksum=CRC32");
        break;
    case 2:
        sprintf(cmd_opt, "--binlog-checksum=NONE");
        break;
    default:
        sprintf(cmd_opt, " ");
    }

    repl->stop_nodes();

    binlog = open_conn_no_db(binlog_port, maxscale_IP, repl->user_name, repl->password, ssl);
    execute_query(binlog, (char *) "stop slave");
    execute_query(binlog, (char *) "reset slave all");
    execute_query(binlog, (char *) "reset master");
    mysql_close(binlog);

    tprintf("Stopping maxscale\n");
    add_result(stop_maxscale(), "Maxscale stopping failed\n");

    for (i = 0; i < repl->N; i++)
    {
        repl->start_node(i, cmd_opt);
    }
    sleep(5);

    tprintf("Connecting to all backend nodes\n");
    repl->connect();

    for (i = 0; i < repl->N; i++)
    {
        execute_query(repl->nodes[i], "stop slave");
        execute_query(repl->nodes[i], "reset slave all");
        execute_query(repl->nodes[i], "reset master");
    }

    prepare_binlog();

    tprintf("Testing binlog when MariaDB is started with '%s' option\n", cmd_opt);

    tprintf("ls binlog data dir on Maxscale node\n");
    add_result(ssh_maxscale(true, "ls -la %s/", maxscale_binlog_dir), "ls failed\n");

    tprintf("show master status\n");
    find_field(repl->nodes[0], (char *) "show master status", (char *) "File", &log_file[0]);
    find_field(repl->nodes[0], (char *) "show master status", (char *) "Position", &log_pos[0]);
    tprintf("Real master file: %s\n", log_file);
    tprintf("Real master pos : %s\n", log_pos);

    tprintf("Stopping first slave (node 1)\n");
    try_query(repl->nodes[1], (char *) "stop slave;");
    //repl->no_set_pos = true;
    repl->no_set_pos = false;
    tprintf("Configure first backend slave node to be slave of real master\n");
    repl->set_slave(repl->nodes[1], repl->IP[0],  repl->port[0], log_file, log_pos);

    tprintf("Starting back Maxscale\n");
    add_result(start_maxscale(), "Maxscale start failed\n");

    tprintf("Connecting to MaxScale binlog router (with any DB)\n");
    binlog = open_conn_no_db(binlog_port, maxscale_IP, repl->user_name, repl->password, ssl);

    add_result(mysql_errno(binlog), "Error connection to binlog router %s\n", mysql_error(binlog));

    repl->no_set_pos = true;
    tprintf("configuring Maxscale binlog router\n");
    repl->set_slave(binlog, repl->IP[0], repl->port[0], log_file, log_pos);

    // ssl between binlog router and Master
    if (backend_ssl)
    {
        sprintf(sys1,
                "CHANGE MASTER TO master_ssl_cert='%s/certs/client-cert.pem', master_ssl_ca='%s/certs/ca.pem', master_ssl=1, master_ssl_key='%s/certs/client-key.pem'",
                maxscale_access_homedir, maxscale_access_homedir, maxscale_access_homedir);
        tprintf("Configuring Master ssl: %s\n", sys1);
        try_query(binlog, sys1);
    }

    try_query(binlog, "start slave");
    try_query(binlog, "show slave status");

    repl->no_set_pos = false;

    // get Master status from Maxscale binlog
    tprintf("show master status\n");
    fflush(stdout);
    find_field(binlog, (char *) "show master status", (char *) "File", &log_file[0]);
    find_field(binlog, (char *) "show master status", (char *) "Position", &log_pos[0]);

    tprintf("Maxscale binlog master file: %s\n", log_file);
    fflush(stdout);
    tprintf("Maxscale binlog master pos : %s\n", log_pos);
    fflush(stdout);

    tprintf("Setup all backend nodes except first one to be slaves of binlog Maxscale node\n");
    fflush(stdout);
    for (i = 2; i < repl->N; i++)
    {
        try_query(repl->nodes[i], (char *) "stop slave;");
        repl->set_slave(repl->nodes[i],  maxscale_IP, binlog_port, log_file, log_pos);
    }
    repl->close_connections();
    try_query(binlog, "show slave status");
    mysql_close(binlog);
    repl->no_set_pos = no_pos;
    return global_result;
}

bool TestConnections::replicate_from_master()
{
    bool rval = true;

    /** Stop the binlogrouter */
    MYSQL* conn = open_conn_no_db(binlog_port, maxscale_IP, repl->user_name, repl->password, ssl);

    if (execute_query(conn, "stop slave"))
    {
        rval = false;
    }
    mysql_close(conn);

    /** Clean up MaxScale directories */
    prepare_binlog();
    ssh_maxscale(true, "service maxscale restart");

    char log_file[256] = "";
    char log_pos[256] = "4";

    repl->execute_query_all_nodes("STOP SLAVE");
    repl->connect();
    execute_query(repl->nodes[0], "RESET MASTER");

    conn = open_conn_no_db(binlog_port, maxscale_IP, repl->user_name, repl->password, ssl);

    if (find_field(repl->nodes[0], "show master status", "File", log_file) ||
        repl->set_slave(conn, repl->IP[0], repl->port[0], log_file, log_pos) ||
        execute_query(conn, "start slave"))
    {
        rval = false;
    }

    mysql_close(conn);

    return rval;
}

int TestConnections::start_mm()
{
    int i;
    char log_file1[256];
    char log_pos1[256];
    char log_file2[256];
    char log_pos2[256];

    tprintf("Stopping maxscale\n");
    fflush(stdout);
    int global_result = stop_maxscale();

    tprintf("Stopping all backend nodes\n");
    fflush(stdout);
    global_result += repl->stop_nodes();

    for (i = 0; i < 2; i++)
    {
        tprintf("Starting back node %d\n", i);
        global_result += repl->start_node(i, (char *) "");
    }

    repl->connect();
    for (i = 0; i < 2; i++)
    {
        execute_query(repl->nodes[i], (char *) "stop slave");
        execute_query(repl->nodes[i], (char *) "reset master");
    }

    execute_query(repl->nodes[0], (char *) "SET GLOBAL READ_ONLY=ON");

    find_field(repl->nodes[0], (char *) "show master status", (char *) "File", log_file1);
    find_field(repl->nodes[0], (char *) "show master status", (char *) "Position", log_pos1);

    find_field(repl->nodes[1], (char *) "show master status", (char *) "File", log_file2);
    find_field(repl->nodes[1], (char *) "show master status", (char *) "Position", log_pos2);

    repl->set_slave(repl->nodes[0], repl->IP[1],  repl->port[1], log_file2, log_pos2);
    repl->set_slave(repl->nodes[1], repl->IP[0],  repl->port[0], log_file1, log_pos1);

    repl->close_connections();

    tprintf("Starting back Maxscale\n");
    fflush(stdout);
    global_result += start_maxscale();

    return global_result;
}

void TestConnections::check_log_err(const char * err_msg, bool expected)
{

    char * err_log_content;

    tprintf("Getting logs\n");
    char sys1[4096];
    set_timeout(100);
    sprintf(&sys1[0], "rm -f *.log; %s %s", get_logs_command, maxscale_IP);
    //tprintf("Executing: %s\n", sys1);
    system(sys1);
    set_timeout(50);

    tprintf("Reading maxscale.log\n");
    if ( ( read_log((char *) "maxscale.log", &err_log_content) != 0) || (strlen(err_log_content) < 2) )
    {
        tprintf("Reading maxscale1.log\n");
        free(err_log_content);
        if (read_log((char *) "maxscale1.log", &err_log_content) != 0)
        {
            add_result(1, "Error reading log\n");
        }
    }
    //printf("\n\n%s\n\n", err_log_content);
    if (err_log_content != NULL)
    {
        if (expected)
        {
            if (strstr(err_log_content, err_msg) == NULL)
            {
                add_result(1, "There is NO \"%s\" error in the log\n", err_msg);
            }
            else
            {
                tprintf("There is proper \"%s \" error in the log\n", err_msg);
            }
        }
        else
        {
            if (strstr(err_log_content, err_msg) != NULL)
            {
                add_result(1, "There is UNEXPECTED error \"%s\" error in the log\n", err_msg);
            }
            else
            {
                tprintf("There are no unxpected errors \"%s \" error in the log\n", err_msg);
            }
        }

        free(err_log_content);
    }
}

int TestConnections::find_connected_slave(int * global_result)
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    repl->connect();
    for (int i = 0; i < repl->N; i++)
    {
        conn_num = get_conn_num(repl->nodes[i], maxscale_ip(), maxscale_hostname, (char *) "test");
        tprintf("connections to %d: %u\n", i, conn_num);
        if ((i == 0) && (conn_num != 1))
        {
            tprintf("There is no connection to master\n");
            *global_result = 1;
        }
        all_conn += conn_num;
        if ((i != 0) && (conn_num != 0))
        {
            current_slave = i;
        }
    }
    if (all_conn != 2)
    {
        tprintf("total number of connections is not 2, it is %d\n", all_conn);
        *global_result = 1;
    }
    tprintf("Now connected slave node is %d (%s)\n", current_slave, repl->IP[current_slave]);
    repl->close_connections();
    return current_slave;
}

int TestConnections::find_connected_slave1()
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    repl->connect();
    for (int i = 0; i < repl->N; i++)
    {
        conn_num = get_conn_num(repl->nodes[i], maxscale_ip(), maxscale_hostname, (char *) "test");
        tprintf("connections to %d: %u\n", i, conn_num);
        all_conn += conn_num;
        if ((i != 0) && (conn_num != 0))
        {
            current_slave = i;
        }
    }
    tprintf("Now connected slave node is %d (%s)\n", current_slave, repl->IP[current_slave]);
    repl->close_connections();
    return current_slave;
}

int TestConnections::check_maxscale_processes(int expected)
{
    char* maxscale_num = ssh_maxscale_output(false, "ps -C maxscale | grep maxscale | wc -l");
    if (maxscale_num == NULL)
    {
        return -1;
    }
    char* nl = strchr(maxscale_num, '\n');
    if (nl)
    {
        *nl = '\0';
    }

    if (atoi(maxscale_num) != expected)
    {
        tprintf("%s maxscale processes detected, trying agin in 5 seconds\n", maxscale_num);
        sleep(5);
        maxscale_num = ssh_maxscale_output(false, "ps -C maxscale | grep maxscale | wc -l");
        if (atoi(maxscale_num) != expected)
        {
            add_result(1, "Number of MaxScale processes is not %d, it is %s\n", expected, maxscale_num);
        }
    }

    return 0;
}

int TestConnections::check_maxscale_alive()
{
    int gr = global_result;
    set_timeout(10);
    tprintf("Connecting to Maxscale\n");
    add_result(connect_maxscale(), "Can not connect to Maxscale\n");
    tprintf("Trying simple query against all sevices\n");
    tprintf("RWSplit \n");
    set_timeout(10);
    try_query(conn_rwsplit, (char *) "show databases;");
    tprintf("ReadConn Master \n");
    set_timeout(10);
    try_query(conn_master, (char *) "show databases;");
    tprintf("ReadConn Slave \n");
    set_timeout(10);
    try_query(conn_slave, (char *) "show databases;");
    set_timeout(10);
    close_maxscale_connections()    ;
    add_result(global_result - gr, "Maxscale is not alive\n");
    stop_timeout();

    check_maxscale_processes(1);

    return global_result - gr;
}

int TestConnections::test_maxscale_connections(bool rw_split, bool rc_master, bool rc_slave)
{
    int rval = 0;
    int rc;

    tprintf("Testing RWSplit, expecting %s\n", (rw_split ? "success" : "failure"));
    rc = execute_query(conn_rwsplit, "select 1");
    if ((rc == 0) != rw_split)
    {
        tprintf("Error: Query %s\n", (rw_split ? "failed" : "succeeded"));
        rval++;
    }

    tprintf("Testing ReadConnRoute Master, expecting %s\n", (rc_master ? "success" : "failure"));
    rc = execute_query(conn_master, "select 1");
    if ((rc == 0) != rc_master)
    {
        tprintf("Error: Query %s", (rc_master ? "failed" : "succeeded"));
        rval++;
    }

    tprintf("Testing ReadConnRoute Slave, expecting %s\n", (rc_slave ? "success" : "failure"));
    rc = execute_query(conn_slave, "select 1");
    if ((rc == 0) != rc_slave)
    {
        tprintf("Error: Query %s", (rc_slave ? "failed" : "succeeded"));
        rval++;
    }
    return rval;
}

void TestConnections::generate_ssh_cmd(char * cmd, char * ssh, bool sudo)
{
    if (strcmp(maxscale_IP, "127.0.0.1") == 0)
    {
        if (sudo)
        {
            sprintf(cmd,
                    "%s %s",
                    maxscale_access_sudo, ssh);
        }
        else
        {
            sprintf(cmd,
                    "%s",
                    ssh);
        }

    }
    else
    {
        if (sudo)
        {
            sprintf(cmd,
                    "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet %s@%s '%s %s'",
                    maxscale_keyfile, maxscale_access_user, maxscale_IP, maxscale_access_sudo, ssh);
        }
        else
        {
            sprintf(cmd,
                    "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet %s@%s '%s\'",
                    maxscale_keyfile, maxscale_access_user, maxscale_IP, ssh);
        }
    }
}


char* TestConnections::ssh_maxscale_output(bool sudo, const char* format, ...)
{
    va_list valist;

    va_start(valist, format);
    int message_len = vsnprintf(NULL, 0, format, valist);
    va_end(valist);

    if (message_len < 0)
    {
        return NULL;
    }

    char *sys = (char*)malloc(message_len + 1);

    va_start(valist, format);
    vsnprintf(sys, message_len + 1, format, valist);
    va_end(valist);

    char *cmd = (char*)malloc(message_len + 1024);
    generate_ssh_cmd(cmd, sys, sudo);
//tprintf("############ssh smd %s\n:", cmd);
    FILE *output = popen(cmd, "r");
    if (output == NULL)
    {
        printf("Error opening ssh %s\n", strerror(errno));
        return NULL;
    }
    char buffer[1024];
    size_t rsize = sizeof(buffer);
    char* result = (char*)calloc(rsize, sizeof(char));

    while (fgets(buffer, sizeof(buffer), output))
    {
        result = (char*)realloc(result, sizeof(buffer) + rsize);
        rsize += sizeof(buffer);
        strcat(result, buffer);
    }

    free(sys);
    free(cmd);
    pclose(output);

    return result;
}

int  TestConnections::ssh_maxscale(bool sudo, const char* format, ...)
{
    va_list valist;

    va_start(valist, format);
    int message_len = vsnprintf(NULL, 0, format, valist);
    va_end(valist);

    if (message_len < 0)
    {
        return -1;
    }

    char *sys = (char*)malloc(message_len + 1);

    va_start(valist, format);
    vsnprintf(sys, message_len + 1, format, valist);
    va_end(valist);

    char *cmd = (char*)malloc(message_len + 1024);

    if (strcmp(maxscale_IP, "127.0.0.1") == 0)
    {
        tprintf("starting bash\n");
        sprintf(cmd, "bash");
    }
    else
    {
        sprintf(cmd,
                "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet %s@%s%s",
                maxscale_keyfile, maxscale_access_user, maxscale_IP, verbose ? "" :  " > /dev/null");
    }
    int rc = 1;
    FILE *in = popen(cmd, "w");

    if (in)
    {
        if (sudo)
        {
            fprintf(in, "sudo su -\n");
            fprintf(in, "cd /home/%s\n", maxscale_access_user);
        }

        fprintf(in, "%s\n", sys);
        rc = pclose(in);
    }

    free(sys);
    free(cmd);
    return rc;
}

int TestConnections::copy_to_maxscale(const char* src, const char* dest)
{
    char sys[strlen(src) + strlen(dest) + 1024];
    if (strcmp(maxscale_IP, "127.0.0.1") == 0)
    {
        sprintf(sys, "cp %s %s",
                src, dest);
    }
    else
    {

        sprintf(sys, "scp -q -i %s -o UserKnownHostsFile=/dev/null "
                     "-o StrictHostKeyChecking=no -o LogLevel=quiet %s %s@%s:%s",
                maxscale_keyfile, src, maxscale_access_user, maxscale_IP, dest);
    }
    return system(sys);
}


int TestConnections::copy_from_maxscale(char* src, char* dest)
{
    char sys[strlen(src) + strlen(dest) + 1024];
    if (strcmp(maxscale_IP, "127.0.0.1") == 0)
    {
        sprintf(sys, "cp %s %s",
                src, dest);
    }
    else
    {
        sprintf(sys, "scp -i %s -o UserKnownHostsFile=/dev/null "
                     "-o StrictHostKeyChecking=no -o LogLevel=quiet %s@%s:%s %s",
                maxscale_keyfile, maxscale_access_user, maxscale_IP, src, dest);
    }
    return system(sys);
}

int TestConnections::create_connections(int conn_N, bool rwsplit_flag, bool master_flag, bool slave_flag,
                                        bool galera_flag)
{
    int i;
    int local_result = 0;
    MYSQL * rwsplit_conn[conn_N];
    MYSQL * master_conn[conn_N];
    MYSQL * slave_conn[conn_N];
    MYSQL * galera_conn[conn_N];


    tprintf("Opening %d connections to each router\n", conn_N);
    for (i = 0; i < conn_N; i++)
    {
        set_timeout(20);

        if (verbose)
        {
            tprintf("opening %d-connection: ", i + 1);
        }

        if (rwsplit_flag)
        {
            if (verbose)
            {
                printf("RWSplit \t");
            }

            rwsplit_conn[i] = open_rwsplit_connection();
            if (!rwsplit_conn[i])
            {
                local_result++;
                tprintf("RWSplit connection failed\n");
            }
        }
        if (master_flag)
        {
            if (verbose)
            {
                printf("ReadConn master \t");
            }

            master_conn[i] = open_readconn_master_connection();
            if ( mysql_errno(master_conn[i]) != 0 )
            {
                local_result++;
                tprintf("ReadConn master connection failed, error: %s\n", mysql_error(master_conn[i]) );
            }
        }
        if (slave_flag)
        {
            if (verbose)
            {
                printf("ReadConn slave \t");
            }

            slave_conn[i] = open_readconn_slave_connection();
            if ( mysql_errno(slave_conn[i]) != 0 )
            {
                local_result++;
                tprintf("ReadConn slave connection failed, error: %s\n", mysql_error(slave_conn[i]) );
            }
        }
        if (galera_flag)
        {
            if (verbose)
            {
                printf("Galera \n");
            }

            galera_conn[i] = open_conn(4016, maxscale_IP, maxscale_user, maxscale_password, ssl);
            if ( mysql_errno(galera_conn[i]) != 0)
            {
                local_result++;
                tprintf("Galera connection failed, error: %s\n", mysql_error(galera_conn[i]));
            }
        }
    }
    for (i = 0; i < conn_N; i++)
    {
        set_timeout(20);

        if (verbose)
        {
            tprintf("Trying query against %d-connection: ", i + 1);
        }

        if (rwsplit_flag)
        {
            if (verbose)
            {
                tprintf("RWSplit \t");
            }
            local_result += execute_query(rwsplit_conn[i], "select 1;");
        }
        if (master_flag)
        {
            if (verbose)
            {
                tprintf("ReadConn master \t");
            }
            local_result += execute_query(master_conn[i], "select 1;");
        }
        if (slave_flag)
        {
            if (verbose)
            {
                tprintf("ReadConn slave \t");
            }
            local_result += execute_query(slave_conn[i], "select 1;");
        }
        if (galera_flag)
        {
            if (verbose)
            {
                tprintf("Galera \n");
            }
            local_result += execute_query(galera_conn[i], "select 1;");
        }
    }

    //global_result += check_pers_conn(Test, pers_conn_expected);
    tprintf("Closing all connections\n");
    for (i = 0; i < conn_N; i++)
    {
        set_timeout(20);
        if (rwsplit_flag)
        {
            mysql_close(rwsplit_conn[i]);
        }
        if (master_flag)
        {
            mysql_close(master_conn[i]);
        }
        if (slave_flag)
        {
            mysql_close(slave_conn[i]);
        }
        if (galera_flag)
        {
            mysql_close(galera_conn[i]);
        }
    }
    stop_timeout();

    return local_result;
}

int TestConnections::get_client_ip(char * ip)
{
    MYSQL * conn;
    MYSQL_RES *res;
    MYSQL_ROW row;
    int ret = 1;
    unsigned long long int num_fields;
    //unsigned long long int row_i=0;
    unsigned long long int rows;
    unsigned long long int i;
    unsigned int conn_num = 0;

    connect_rwsplit();
    if (execute_query(conn_rwsplit, (char *) "CREATE DATABASE IF NOT EXISTS db_to_check_clent_ip") != 0 )
    {
        return ret;
    }
    close_rwsplit();
    conn = open_conn_db(rwsplit_port, maxscale_IP, (char *) "db_to_check_clent_ip", maxscale_user,
                        maxscale_password, ssl);

    if (conn != NULL)
    {
        if (mysql_query(conn, "show processlist;") != 0)
        {
            printf("Error: can't execute SQL-query: show processlist\n");
            printf("%s\n\n", mysql_error(conn));
            conn_num = 0;
        }
        else
        {
            res = mysql_store_result(conn);
            if (res == NULL)
            {
                printf("Error: can't get the result description\n");
                conn_num = -1;
            }
            else
            {
                num_fields = mysql_num_fields(res);
                rows = mysql_num_rows(res);
                for (i = 0; i < rows; i++)
                {
                    row = mysql_fetch_row(res);
                    if ( (row[2] != NULL ) && (row[3] != NULL) )
                    {
                        if  (strstr(row[3], "db_to_check_clent_ip") != NULL)
                        {
                            ret = 0;
                            strcpy(ip, row[2]);
                        }
                    }
                }
            }
            mysql_free_result(res);
        }
    }

    mysql_close(conn);
    return ret;
}

int TestConnections::set_timeout(long int timeout_seconds)
{
    if (enable_timeouts)
    {
        timeout = timeout_seconds;
    }
    return 0;
}

int TestConnections::set_log_copy_interval(long int interval_seconds)
{
    log_copy_to_go = interval_seconds;
    log_copy_interval = interval_seconds;
    return 0;
}

int TestConnections::stop_timeout()
{
    timeout = 999999999;
    return 0;
}

int TestConnections::tprintf(const char *format, ...)
{
    timeval t2;
    gettimeofday(&t2, NULL);
    double elapsedTime = (t2.tv_sec - start_time.tv_sec);
    elapsedTime += (double) (t2.tv_usec - start_time.tv_usec) / 1000000.0;

    struct tm tm_now;
    localtime_r(&t2.tv_sec, &tm_now);
    unsigned int msec = t2.tv_usec / 1000;

    printf("%02u:%02u:%02u.%03u %04f: ", tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, msec, elapsedTime);

    va_list argp;
    va_start(argp, format);
    vprintf(format, argp);
    va_end(argp);

    /** Add a newline if the message doesn't have one */
    if (format[strlen(format) - 1] != '\n')
    {
        printf("\n");
    }

    fflush(stdout);
    fflush(stderr);
}

void *timeout_thread( void *ptr )
{
    TestConnections * Test = (TestConnections *) ptr;
    struct timespec tim;
    while (Test->timeout > 0)
    {
        tim.tv_sec = 1;
        tim.tv_nsec = 0;
        nanosleep(&tim, NULL);
        Test->timeout--;
    }
    Test->tprintf("\n **** Timeout! *** \n");
    delete Test;
    exit(250);
}

void *log_copy_thread( void *ptr )
{
    TestConnections * Test = (TestConnections *) ptr;
    struct timespec tim;
    while (true)
    {
        while (Test->log_copy_to_go > 0)
        {
            tim.tv_sec = 1;
            tim.tv_nsec = 0;
            nanosleep(&tim, NULL);
            Test->log_copy_to_go--;
        }
        Test->log_copy_to_go = Test->log_copy_interval;
        Test->tprintf("\n **** Copying all logs *** \n");
        Test->copy_all_logs_periodic();
    }
}

int TestConnections::insert_select(int N)
{
    int result = 0;

    tprintf("Create t1\n");
    set_timeout(30);
    create_t1(conn_rwsplit);

    tprintf("Insert data into t1\n");
    set_timeout(N * 16 + 30);
    insert_into_t1(conn_rwsplit, N);
    stop_timeout();
    repl->sync_slaves();

    tprintf("SELECT: rwsplitter\n");
    set_timeout(30);
    result += select_from_t1(conn_rwsplit, N);

    tprintf("SELECT: master\n");
    set_timeout(30);
    result += select_from_t1(conn_master, N);

    tprintf("SELECT: slave\n");
    set_timeout(30);
    result += select_from_t1(conn_slave, N);

    return result;
}

int TestConnections::use_db(char * db)
{
    int local_result = 0;
    char sql[100];

    sprintf(sql, "USE %s;", db);
    set_timeout(20);
    tprintf("selecting DB '%s' for rwsplit\n", db);
    local_result += execute_query(conn_rwsplit, sql);
    tprintf("selecting DB '%s' for readconn master\n", db);
    local_result += execute_query(conn_slave, sql);
    tprintf("selecting DB '%s' for readconn slave\n", db);
    local_result += execute_query(conn_master, sql);
    for (int i = 0; i < repl->N; i++)
    {
        tprintf("selecting DB '%s' for direct connection to node %d\n", db, i);
        local_result += execute_query(repl->nodes[i], sql);
    }
    return local_result;
}

int TestConnections::check_t1_table(bool presence, char * db)
{
    const char *expected = presence ? "" : "NOT";
    const char *actual = presence ? "NOT" : "";
    int start_result = global_result;

    add_result(use_db(db), "use db failed\n");
    stop_timeout();
    repl->sync_slaves();

    tprintf("Checking: table 't1' should %s be found in '%s' database\n", expected, db);
    set_timeout(30);
    int exists = check_if_t1_exists(conn_rwsplit);

    if (exists == presence)
    {
        tprintf("RWSplit: ok\n");
    }
    else
    {
        add_result(1, "Table t1 is %s found in '%s' database using RWSplit\n", actual, db);
    }

    set_timeout(30);
    exists = check_if_t1_exists(conn_master);

    if (exists == presence)
    {
        tprintf("ReadConn master: ok\n");
    }
    else
    {
        add_result(1, "Table t1 is %s found in '%s' database using Readconnrouter with router option master\n",
                   actual, db);
    }

    set_timeout(30);
    exists = check_if_t1_exists(conn_slave);

    if (exists == presence)
    {
        tprintf("ReadConn slave: ok\n");
    }
    else
    {
        add_result(1, "Table t1 is %s found in '%s' database using Readconnrouter with router option slave\n", actual,
                   db);
    }


    for (int i = 0; i < repl->N; i++)
    {
        set_timeout(30);
        exists = check_if_t1_exists(repl->nodes[i]);
        if (exists == presence)
        {
            tprintf("Node %d: ok\n", i);
        }
        else
        {
            add_result(1, "Table t1 is %s found in '%s' database using direct connect to node %d\n", actual, db, i);
        }
    }

    stop_timeout();

    return global_result - start_result;
}

int TestConnections::try_query(MYSQL *conn, const char *format, ...)
{
    va_list valist;

    va_start(valist, format);
    int message_len = vsnprintf(NULL, 0, format, valist);
    va_end(valist);

    char sql[message_len + 1];

    va_start(valist, format);
    vsnprintf(sql, sizeof(sql), format, valist);
    va_end(valist);

    int res = execute_query1(conn, sql, false);
    add_result(res, "Query '%.*s%s' failed!\n", message_len < 100 ? message_len : 100, sql, message_len < 100 ? "" : "...");
    return res;
}

int TestConnections::try_query_all(const char *sql)
{
    return try_query(conn_rwsplit, sql) +
           try_query(conn_master, sql) +
           try_query(conn_slave, sql);
}

int TestConnections::find_master_maxadmin(Mariadb_nodes * nodes)
{
    bool found = false;
    int master = -1;

    for (int i = 0; i < nodes->N; i++)
    {
        char show_server[256];
        char res[256];
        sprintf(show_server, "show server server%d", i + 1);
        get_maxadmin_param(show_server, (char *) "Status", res);

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

int TestConnections::find_slave_maxadmin(Mariadb_nodes * nodes)
{
    int slave = -1;

    for (int i = 0; i < nodes->N; i++)
    {
        char show_server[256];
        char res[256];
        sprintf(show_server, "show server server%d", i + 1);
        get_maxadmin_param(show_server, (char *) "Status", res);

        if (strstr(res, "Slave"))
        {
            slave = i;
        }
    }

    return slave;
}

int TestConnections::execute_maxadmin_command(char * cmd)
{
    return ssh_maxscale(true, "maxadmin %s", cmd);
}
int TestConnections::execute_maxadmin_command_print(char * cmd)
{
    printf("%s\n", ssh_maxscale_output(true, "maxadmin %s", cmd));
    return 0;
}

int TestConnections::check_maxadmin_param(const char *command, const char *param, const char *value)
{
    char result[1024];
    int rval = 1;

    if (get_maxadmin_param((char*)command, (char*)param, (char*)result) == 0)
    {
        char *end = strchr(result, '\0') - 1;

        while (isspace(*end))
        {
            *end-- = '\0';
        }

        char *start = result;

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

int TestConnections::get_maxadmin_param(const char *command, const char *param, char *result)
{
    char        * buf;

    buf = ssh_maxscale_output(true, "maxadmin %s", command);

    //printf("%s\n", buf);

    char *x = strstr(buf, param);

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

    char *end = strchr(x, '\n');

    // Trim trailing whitespace
    while (isspace(*end))
    {
        *end-- = '\0';
    }

    strcpy(result, x);

    return 0;
}

int TestConnections::list_dirs()
{
    for (int i = 0; i < repl->N; i++)
    {
        tprintf("ls on node %d\n", i);
        repl->ssh_node(i, (char *) "ls -la /var/lib/mysql", true);
        fflush(stdout);
    }
    tprintf("ls maxscale \n");
    ssh_maxscale(true, "ls -la /var/lib/maxscale/");
    fflush(stdout);
    return 0;
}

long unsigned TestConnections::get_maxscale_memsize()
{
    char * ps_out = ssh_maxscale_output(false, "ps -e -o pid,vsz,comm= | grep maxscale");
    long unsigned mem = 0;
    pid_t pid;
    sscanf(ps_out, "%d %lu", &pid, &mem);
    return mem;
}

void TestConnections::check_current_operations(int value)
{
    char value_str[512];
    sprintf(value_str, "%d", value);

    for (int i = 0; i < repl->N; i++)
    {
        char command[512];
        sprintf(command, "show server server%d", i + 1);
        add_result(check_maxadmin_param(command, "Current no. of operations:", value_str),
                   "Current no. of operations is not %s", value_str);
    }
}

void TestConnections::check_current_connections(int value)
{
    char value_str[512];
    sprintf(value_str, "%d", value);

    for (int i = 0; i < repl->N; i++)
    {
        char command[512];
        sprintf(command, "show server server%d", i + 1);
        add_result(check_maxadmin_param(command, "Current no. of conns:", value_str),
                   "Current no. of conns is not %s", value_str);
    }
}

int TestConnections::take_snapshot(char * snapshot_name)
{
    char str[4096];
    sprintf(str, "%s %s", take_snapshot_command, snapshot_name);
    return system(str);
}

int TestConnections::revert_snapshot(char * snapshot_name)
{
    char str[4096];
    sprintf(str, "%s %s", revert_snapshot_command, snapshot_name);
    return system(str);
}

bool TestConnections::test_bad_config(const char *config)
{
    char src[PATH_MAX];

    process_template(config, "./");

    // Set the timeout to prevent hangs with configurations that work
    set_timeout(20);

    return ssh_maxscale(true, "cp maxscale.cnf /etc/maxscale.cnf; service maxscale stop; "
                        "maxscale -U maxscale -lstdout &> /dev/null && sleep 1 && pkill -9 maxscale") == 0;
}

int TestConnections::connect_rwsplit()
{
    if (use_ipv6)
    {
        conn_rwsplit = open_conn(rwsplit_port, maxscale_IP6, maxscale_user, maxscale_password, ssl);
    }
    else
    {
        conn_rwsplit = open_conn(rwsplit_port, maxscale_IP, maxscale_user, maxscale_password, ssl);
    }
    routers[0] = conn_rwsplit;

    int rc = 0;
    int my_errno = mysql_errno(conn_rwsplit);

    if (my_errno)
    {
        if (verbose)
        {
            tprintf("Failed to connect to readwritesplit: %d, %s", my_errno, mysql_error(conn_rwsplit));
        }
        rc = my_errno;
    }

    return rc;
}

int TestConnections::connect_readconn_master()
{
    if (use_ipv6)
    {
        conn_master = open_conn(readconn_master_port, maxscale_IP6, maxscale_user, maxscale_password, ssl);
    }
    else
    {
        conn_master = open_conn(readconn_master_port, maxscale_IP, maxscale_user, maxscale_password, ssl);
    }
    routers[1] = conn_master;

    int rc = 0;
    int my_errno = mysql_errno(conn_master);

    if (my_errno)
    {
        if (verbose)
        {
            tprintf("Failed to connect to readwritesplit: %d, %s", my_errno, mysql_error(conn_master));
        }
        rc = my_errno;
    }

    return rc;
}

int TestConnections::connect_readconn_slave()
{
    if (use_ipv6)
    {
        conn_slave = open_conn(readconn_slave_port, maxscale_IP6, maxscale_user, maxscale_password, ssl);
    }
    else
    {
        conn_slave = open_conn(readconn_slave_port, maxscale_IP, maxscale_user, maxscale_password, ssl);
    }
    routers[2] = conn_slave;

    int rc = 0;
    int my_errno = mysql_errno(conn_slave);

    if (my_errno)
    {
        if (verbose)
        {
            tprintf("Failed to connect to readwritesplit: %d, %s", my_errno, mysql_error(conn_slave));
        }
        rc = my_errno;
    }

    return rc;
}

char* TestConnections::maxscale_ip() const
{
    return use_ipv6 ?  (char*)maxscale_IP6 : (char*)maxscale_IP;
}

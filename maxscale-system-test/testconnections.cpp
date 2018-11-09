#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <execinfo.h>
#include <sys/stat.h>
#include <iostream>
#include <sstream>
#include <maxbase/stacktrace.hh>

#include "mariadb_func.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"
#include "testconnections.h"

using namespace mxb;

namespace maxscale
{

static bool start = true;
static bool check_nodes = true;
static bool manual_debug = false;
static std::string required_repl_version;
static std::string required_galera_version;
static bool restart_galera = false;
static bool multiple_maxscales = false;
}

static void perform_manual_action(const char* zMessage)
{
    std::cout << zMessage << " (press enter when done)." << std::endl;
    std::string not_used;
    std::getline(std::cin, not_used);
    std::cout << "Ok" << std::endl;
}

static void signal_set(int sig, void (* handler)(int))
{
    struct sigaction sigact = {};
    sigact.sa_handler = handler;

    do
    {
        errno = 0;
        sigaction(sig, &sigact, NULL);
    }
    while (errno == EINTR);
}

void sigfatal_handler(int i)
{
    dump_stacktrace();
    signal_set(i, SIG_DFL);
    raise(i);
}

void TestConnections::check_nodes(bool value)
{
    maxscale::check_nodes = value;
}

void TestConnections::skip_maxscale_start(bool value)
{
    maxscale::start = !value;
}

void TestConnections::multiple_maxscales(bool value)
{
    maxscale::multiple_maxscales = value;
}

void TestConnections::require_repl_version(const char* version)
{
    maxscale::required_repl_version = version;
}

void TestConnections::require_galera_version(const char* version)
{
    maxscale::required_galera_version = version;
}

void TestConnections::restart_galera(bool value)
{
    maxscale::restart_galera = value;
}

TestConnections::TestConnections(int argc, char* argv[])
    : enable_timeouts(true)
    , global_result(0)
    , use_snapshots(false)
    , local_maxscale(false)
    , no_backend_log_copy(false)
    , no_maxscale_log_copy(false)
    , verbose(false)
    , smoke(true)
    , binlog_cmd_option(0)
    , ssl(false)
    , backend_ssl(false)
    , binlog_master_gtid(false)
    , binlog_slave_gtid(false)
    , no_galera(false)
    , no_vm_revert(true)
    , threads(4)
    , use_ipv6(false)
{
    signal_set(SIGSEGV, sigfatal_handler);
    signal_set(SIGABRT, sigfatal_handler);
    signal_set(SIGFPE, sigfatal_handler);
    signal_set(SIGILL, sigfatal_handler);
#ifdef SIGBUS
    signal_set(SIGBUS, sigfatal_handler);
#endif
    gettimeofday(&start_time, NULL);

    read_env();

    char* gal_env = getenv("galera_000_network");
    if ((gal_env == NULL) || (strcmp(gal_env, "") == 0 ))
    {
        no_galera = true;
        tprintf("Galera backend variables are not defined, Galera won't be used\n");
    }

    bool maxscale_init = true;

    static struct option long_options[] =
    {

        {"help",              no_argument,              0,                        'h'                },
        {"verbose",           no_argument,              0,                        'v'                },
        {"silent",            no_argument,              0,                        'n'                },
        {"quiet",             no_argument,              0,                        'q'                },
        {"no-maxscale-start", no_argument,              0,                        's'                },
        {"no-maxscale-init",  no_argument,              0,                        'i'                },
        {"no-nodes-check",    no_argument,              0,                        'r'                },
        {"restart-galera",    no_argument,              0,                        'g'                },
        {"no-timeouts",       no_argument,              0,                        'z'                },
        {"no-galera",         no_argument,              0,                        'y'                },
        {"local-maxscale",    optional_argument,        0,                        'l'                },
        {0,                   0,                        0,                        0                  }
    };

    int c;
    int option_index = 0;

    while ((c = getopt_long(argc, argv, "hvnqsirgzyl::", long_options, &option_index)) != -1)
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
            {
                printf("Options:\n");

                struct option* o = long_options;

                while (o->name)
                {
                    printf("-%c, --%s\n", o->val, o->name);
                    ++o;
                }
                exit(0);
            }
            break;

        case 's':
            printf("Maxscale won't be started\n");
            maxscale::start = false;
            maxscale::manual_debug = true;
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
            maxscale::restart_galera = true;
            break;

        case 'z':
            enable_timeouts = false;
            break;

        case 'y':
            printf("Do not use Galera setup\n");
            no_galera = true;
            break;

        case 'l':
            {
                const char* local_ip = optarg ? optarg : "127.0.0.1";
                printf(
                    "MaxScale assumed to be running locally; not started and logs not downloaded. IP: %s\n",
                    local_ip);

                maxscale_init = false;
                no_maxscale_log_copy = true;
                local_maxscale = true;

                setenv("maxscale_IP", local_ip, true);
                setenv("maxscale_network", local_ip, true);
                setenv("maxscale_private_ip", local_ip, true);
            }
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

    sprintf(ssl_options,
            "--ssl-cert=%s/ssl-cert/client-cert.pem --ssl-key=%s/ssl-cert/client-key.pem",
            test_dir,
            test_dir);
    setenv("ssl_options", ssl_options, 1);

    repl = new Mariadb_nodes("node", test_dir, verbose);
    if (!no_galera)
    {
        galera = new Galera_nodes("galera", test_dir, verbose);
        // galera->use_ipv6 = use_ipv6;
        galera->use_ipv6 = false;
        galera->take_snapshot_command = take_snapshot_command;
        galera->revert_snapshot_command = revert_snapshot_command;
    }
    else
    {
        galera = NULL;
    }

    repl->use_ipv6 = use_ipv6;
    repl->take_snapshot_command = take_snapshot_command;
    repl->revert_snapshot_command = revert_snapshot_command;

    maxscales = new Maxscales("maxscale", test_dir, verbose);

    maxscales->use_ipv6 = use_ipv6;
    maxscales->ssl = ssl;

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

    if ((maxscale::restart_galera) && (!no_galera))
    {
        galera->stop_nodes();
        galera->start_replication();
    }

    bool snapshot_reverted = false;

    if (use_snapshots)
    {
        snapshot_reverted = revert_snapshot((char*) "clean");
    }

    if (!snapshot_reverted && maxscale::check_nodes
        && (repl->check_replication() || (!no_galera && galera->check_replication())))
    {
        // Stop MaxScale to prevent it from interfering with the replication setup process
        if (!maxscale::manual_debug)
        {
            maxscales->stop_all();
        }

        if (!repl->fix_replication())
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
        init_maxscales();
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
    pthread_create(&timeout_thread_p, NULL, timeout_thread, this);
    pthread_create(&log_copy_thread_p, NULL, log_copy_thread, this);
    tprintf("Starting test");
    gettimeofday(&start_time, NULL);
}

TestConnections::~TestConnections()
{
    if (backend_ssl)
    {
        repl->disable_ssl();
        // galera->disable_ssl();
    }

    copy_all_logs();

    /* Temporary disable snapshot revert due to Galera failures
     *  if (global_result != 0 )
     *  {
     *   if (no_vm_revert)
     *   {
     *       tprintf("no_vm_revert flag is set, not reverting VMs\n");
     *   }
     *   else
     *   {
     *       tprintf("Reverting snapshot\n");
     *       revert_snapshot((char*) "clean");
     *   }
     *  }
     */

    delete repl;
    if (!no_galera)
    {
        delete galera;
    }

    if (maxscale::multiple_maxscales)
    {
        maxscales->stop_all();
    }

    if (global_result)
    {
        // This causes the test to fail if a core dump is found
        exit(1);
    }
}

void TestConnections::report_result(const char* format, va_list argp)
{
    timeval t2;
    gettimeofday(&t2, NULL);
    double elapsedTime = (t2.tv_sec - start_time.tv_sec);
    elapsedTime += (double) (t2.tv_usec - start_time.tv_usec) / 1000000.0;

    global_result += 1;

    printf("%04f: TEST_FAILED! ", elapsedTime);

    vprintf(format, argp);

    if (format[strlen(format) - 1] != '\n')
    {
        printf("\n");
    }
}

void TestConnections::add_result(bool result, const char* format, ...)
{
    if (result)
    {
        va_list argp;
        va_start(argp, format);
        report_result(format, argp);
        va_end(argp);
    }
}

void TestConnections::expect(bool result, const char* format, ...)
{
    if (!result)
    {
        va_list argp;
        va_start(argp, format);
        report_result(format, argp);
        va_end(argp);
    }
}

void TestConnections::read_env()
{

    char* env;

    if (verbose)
    {
        printf("Reading test setup configuration from environmental variables\n");
    }


    // env = getenv("get_logs_command"); if (env != NULL) {sprintf(get_logs_command, "%s", env);}

    env = getenv("sysbench_dir");
    if (env != NULL)
    {
        sprintf(sysbench_dir, "%s", env);
    }

    // env = getenv("test_dir"); if (env != NULL) {sprintf(test_dir, "%s", env);}

    ssl = false;
    env = getenv("ssl");
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0)))
    {
        ssl = true;
    }
    env = getenv("mysql51_only");
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0)))
    {
        maxscale::check_nodes = false;
    }

    env = getenv("no_nodes_check");
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0)))
    {
        maxscale::check_nodes = false;
    }
    env = getenv("no_backend_log_copy");
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0)))
    {
        no_backend_log_copy = true;
    }
    env = getenv("no_maxscale_log_copy");
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0)))
    {
        no_maxscale_log_copy = true;
    }
    env = getenv("use_ipv6");
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0)))
    {
        use_ipv6 = true;
    }

    env = getenv("backend_ssl");
    if (env != NULL && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0)))
    {
        backend_ssl = true;
    }
    else
    {
        backend_ssl = false;
    }

    env = getenv("smoke");
    if (env)
    {
        smoke = strcasecmp(env, "yes") == 0 || strcasecmp(env, "true") == 0;
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
    if (env != NULL && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0)))
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
    if (env != NULL && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0)))
    {
        maxscale::start = false;
    }

    env = getenv("no_vm_revert");
    if ((env != NULL) && ((strcasecmp(env, "no") == 0) || (strcasecmp(env, "false") == 0)))
    {
        no_vm_revert = false;
    }
}

void TestConnections::print_env()
{
    printf("Maxscale IP\t%s\n", maxscales->IP[0]);
    printf("Maxscale User name\t%s\n", maxscales->user_name);
    printf("Maxscale Password\t%s\n", maxscales->password);
    printf("Maxscale SSH key\t%s\n", maxscales->sshkey[0]);
    printf("Maxadmin password\t%s\n", maxscales->maxadmin_password[0]);
    printf("Access user\t%s\n", maxscales->access_user[0]);
    if (repl)
    {
        repl->print_env();
    }
    if (galera)
    {
        galera->print_env();
    }
}

const char* get_template_name(char* test_name)
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

    printf("Failed to find configuration template for test '%s', using default template '%s'.\n",
           test_name,
           default_template);
    return default_template;
}

void TestConnections::process_template(int m, const char* template_name, const char* dest)
{
    struct stat stb;
    char str[4096];
    char template_file[1024];

    char extended_template_file[1024];

    sprintf(template_file, "%s/cnf/maxscale.cnf.template.%s", test_dir, template_name);
    sprintf(extended_template_file, "%s.%03d", template_file, m);

    if (stat(extended_template_file, &stb) == 0)
    {
        strcpy(template_file, extended_template_file);
    }
    tprintf("Template file is %s\n", template_file);

    std::stringstream ss;

    ss << "sed ";

    if (backend_ssl)
    {
        ss
            <<
            " -e \"s|type=server|type=server\\nssl=required\\nssl_cert=/###access_homedir###/certs/client-cert.pem\\nssl_key=/###access_homedir###/certs/client-key.pem\\nssl_ca_cert=/###access_homedir###/certs/ca.pem|g\" ";
    }

    ss << " -e \"s/###threads###/" << threads << "/\" ";

    Mariadb_nodes* mdn[2] {repl, galera};
    char* IPcnf;
    int mdn_n = galera ? 2 : 1;

    for (int j = 0; j < mdn_n; j++)
    {
        for (int i = 0; i < mdn[j]->N; i++)
        {
            if (mdn[j]->use_ipv6)
            {
                IPcnf = mdn[j]->IP6[i];
            }
            else
            {
                IPcnf = mdn[j]->IP[i];
            }
            sprintf(str,
                    " -e \"s/###%s_server_IP_%0d###/%s/\" ",
                    mdn[j]->prefix,
                    i + 1,
                    IPcnf);
            ss << str;

            sprintf(str,
                    " -e \"s/###%s_server_port_%0d###/%d/\" ",
                    mdn[j]->prefix,
                    i + 1,
                    mdn[j]->port[i]);
            ss << str;
        }

        mdn[j]->connect();
        execute_query(mdn[j]->nodes[0], "CREATE DATABASE IF NOT EXISTS test");
        mdn[j]->close_connections();
    }

    sprintf(str, " -e \"s/###access_user###/%s/g\" ", maxscales->access_user[m]);
    ss << str;

    sprintf(str, " -e \"s|###access_homedir###|%s|g\" ", maxscales->access_homedir[m]);
    ss << str;

    ss << template_file << " > maxscale.cnf";
    system(ss.str().c_str());

    maxscales->copy_to_node_legacy("maxscale.cnf", dest, m);
    // The config will now be in ~/maxscale.cnf and is moved into /etc before restarting maxscale
}

void TestConnections::init_maxscales()
{
    // Always initialize the first MaxScale
    init_maxscale(0);

    if (maxscale::multiple_maxscales)
    {
        for (int i = 1; i < maxscales->N; i++)
        {
            init_maxscale(i);
        }
    }
}

void TestConnections::init_maxscale(int m)
{
    const char* template_name = get_template_name(test_name);

    process_template(m, template_name, maxscales->access_homedir[m]);

    if (maxscales->ssh_node_f(m, true, "test -d %s/certs", maxscales->access_homedir[m]))
    {
        tprintf("SSL certificates not found, copying to maxscale");
        maxscales->ssh_node_f(m,
                              true,
                              "rm -rf %s/certs;mkdir -m a+wrx %s/certs;",
                              maxscales->access_homedir[m],
                              maxscales->access_homedir[m]);

        char str[4096];
        char dtr[4096];
        sprintf(str, "%s/ssl-cert/*", test_dir);
        sprintf(dtr, "%s/certs/", maxscales->access_homedir[m]);
        maxscales->copy_to_node_legacy(str, dtr, m);
        sprintf(str, "cp %s/ssl-cert/* .", test_dir);
        system(str);
        maxscales->ssh_node_f(m, true, "chmod -R a+rx %s;", maxscales->access_homedir[m]);
    }

    maxscales->ssh_node_f(m,
                          true,
                          "cp maxscale.cnf %s;"
                          "iptables -F INPUT;"
                          "rm -rf %s/*.log /tmp/core* /dev/shm/* /var/lib/maxscale/maxscale.cnf.d/ /var/lib/maxscale/*;"
                          "%s"
                          "maxctrl api get maxscale/debug/monitor_wait",
                          maxscales->maxscale_cnf[m],
                          maxscales->maxscale_log_dir[m],
                          maxscale::start ? "service maxscale restart;" : "");
}

void TestConnections::copy_one_mariadb_log(int i, std::string filename)
{
    int exit_code;
    char* mariadb_log = repl->ssh_node_output(i, "cat /var/lib/mysql/*.err 2>/dev/null", true, &exit_code);
    FILE* f = fopen(filename.c_str(), "w");

    if (f != NULL)
    {
        fwrite(mariadb_log, sizeof(char), strlen(mariadb_log), f);
        fclose(f);
    }

    free(mariadb_log);
}

int TestConnections::copy_mariadb_logs(Mariadb_nodes* repl,
                                       const char* prefix,
                                       std::vector<std::thread>& threads)
{
    int local_result = 0;

    if (repl)
    {
        for (int i = 0; i < repl->N; i++)
        {
            if (strcmp(repl->IP[i], "127.0.0.1") != 0)      // Do not copy MariaDB logs in case of local
                                                            // backend
            {
                char str[4096];
                sprintf(str, "LOGS/%s/%s%d_mariadb_log", test_name, prefix, i);
                threads.emplace_back(&TestConnections::copy_one_mariadb_log, this, i, str);
            }
        }
    }

    return local_result;
}

int TestConnections::copy_all_logs()
{
    set_timeout(300);

    char str[PATH_MAX + 1];
    sprintf(str, "mkdir -p LOGS/%s", test_name);
    system(str);

    std::vector<std::thread> threads;

    if (!no_backend_log_copy)
    {
        copy_mariadb_logs(repl, "node", threads);
        copy_mariadb_logs(galera, "galera", threads);
    }

    int rv = 0;

    if (!no_maxscale_log_copy)
    {
        rv = copy_maxscale_logs(0);
    }

    for (auto& a : threads)
    {
        a.join();
    }

    return rv;
}
int TestConnections::copy_maxscale_logs(double timestamp)
{
    char log_dir[1024];
    char log_dir_i[1024];
    char sys[1024];
    if (timestamp == 0)
    {
        sprintf(log_dir, "LOGS/%s", test_name);
    }
    else
    {
        sprintf(log_dir, "LOGS/%s/%04f", test_name, timestamp);
    }
    for (int i = 0; i < maxscales->N; i++)
    {
        sprintf(log_dir_i, "%s/%03d", log_dir, i);
        sprintf(sys, "mkdir -p %s", log_dir_i);
        system(sys);
        if (strcmp(maxscales->IP[i], "127.0.0.1") != 0)
        {
            int rc = maxscales->ssh_node_f(i, true,
                                  "rm -rf %s/logs;"
                                  "mkdir %s/logs;"
                                  "cp %s/*.log %s/logs/;"
                                  "cp /tmp/core* %s/logs/;"
                                  "cp %s %s/logs/;"
                                  "chmod 777 -R %s/logs;"
                                  "ls /tmp/core* && exit 42;",
                                  maxscales->access_homedir[i],
                                  maxscales->access_homedir[i],
                                  maxscales->maxscale_log_dir[i],
                                  maxscales->access_homedir[i],
                                  maxscales->access_homedir[i],
                                  maxscales->maxscale_cnf[i],
                                  maxscales->access_homedir[i],
                                  maxscales->access_homedir[i]);
            sprintf(sys, "%s/logs/*", maxscales->access_homedir[i]);
            maxscales->copy_from_node(i, sys, log_dir_i);
            expect(rc != 42, "Test should not generate core files");
        }
        else
        {
            maxscales->ssh_node_f(i, true, "cp %s/*.logs %s/", maxscales->maxscale_log_dir[i], log_dir_i);
            maxscales->ssh_node_f(i, true, "cp /tmp/core* %s/", log_dir_i);
            maxscales->ssh_node_f(i, true, "cp %s %s/", maxscales->maxscale_cnf[i], log_dir_i);
            maxscales->ssh_node_f(i, true, "chmod a+r -R %s", log_dir_i);
        }
    }
    return 0;
}

int TestConnections::copy_all_logs_periodic()
{
    timeval t2;
    gettimeofday(&t2, NULL);
    double elapsedTime = (t2.tv_sec - start_time.tv_sec);
    elapsedTime += (double) (t2.tv_usec - start_time.tv_usec) / 1000000.0;

    return copy_maxscale_logs(elapsedTime);
}

int TestConnections::prepare_binlog(int m)
{
    char version_str[1024] = "";

    repl->connect();
    find_field(repl->nodes[0], "SELECT @@version", "@@version", version_str);
    tprintf("Master server version '%s'", version_str);

    if (*version_str
        && strstr(version_str, "10.0") == NULL
        && strstr(version_str, "10.1") == NULL
        && strstr(version_str, "10.2") == NULL)
    {
        add_result(maxscales->ssh_node_f(m,
                                         true,
                                         "sed -i \"s/,mariadb10-compatibility=1//\" %s",
                                         maxscales->maxscale_cnf[m]),
                   "Error editing maxscale.cnf");
    }

    if (!local_maxscale)
    {
        tprintf("Removing all binlog data from Maxscale node");
        add_result(maxscales->ssh_node_f(m, true, "rm -rf %s", maxscales->maxscale_binlog_dir[m]),
                   "Removing binlog data failed");

        tprintf("Creating binlog dir");
        add_result(maxscales->ssh_node_f(m, true, "mkdir -p %s", maxscales->maxscale_binlog_dir[m]),
                   "Creating binlog data dir failed");
        tprintf("Set 'maxscale' as a owner of binlog dir");
        add_result(maxscales->ssh_node_f(m,
                                         false,
                                         "%s mkdir -p %s; %s chown maxscale:maxscale -R %s",
                                         maxscales->access_sudo[m],
                                         maxscales->maxscale_binlog_dir[m],
                                         maxscales->access_sudo[m],
                                         maxscales->maxscale_binlog_dir[m]),
                   "directory ownership change failed");
    }
    else
    {
        perform_manual_action("Remove all local binlog data");
    }

    return 0;
}

int TestConnections::start_binlog(int m)
{
    char sys1[4096];
    MYSQL* binlog;
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

    if (!local_maxscale)
    {
        binlog =
            open_conn_no_db(maxscales->binlog_port[m], maxscales->IP[m], repl->user_name, repl->password,
                            ssl);
        execute_query(binlog, "stop slave");
        execute_query(binlog, "reset slave all");
        mysql_close(binlog);

        tprintf("Stopping maxscale\n");
        add_result(maxscales->stop_maxscale(m), "Maxscale stopping failed\n");
    }
    else
    {
        perform_manual_action(
            "Perform the equivalent of 'STOP SLAVE; RESET SLAVE ALL' and stop local Maxscale");
    }

    for (i = 0; i < repl->N; i++)
    {
        repl->start_node(i, cmd_opt);
    }
    sleep(5);

    tprintf("Connecting to all backend nodes\n");
    repl->connect();

    tprintf("Stopping everything\n");
    for (i = 0; i < repl->N; i++)
    {
        execute_query(repl->nodes[i], "stop slave");
        execute_query(repl->nodes[i], "reset slave all");
        execute_query(repl->nodes[i], "reset master");
    }

    prepare_binlog(m);

    tprintf("Testing binlog when MariaDB is started with '%s' option\n", cmd_opt);

    if (!local_maxscale)
    {
        tprintf("ls binlog data dir on Maxscale node\n");
        add_result(maxscales->ssh_node_f(m, true, "ls -la %s/", maxscales->maxscale_binlog_dir[m]),
                   "ls failed\n");
    }

    if (binlog_master_gtid)
    {
        // GTID to connect real Master
        tprintf("GTID for connection 1st slave to master!\n");
        try_query(repl->nodes[1], "stop slave");
        try_query(repl->nodes[1], "SET @@global.gtid_slave_pos=''");
        sprintf(sys1,
                "CHANGE MASTER TO MASTER_HOST='%s', MASTER_PORT=%d, MASTER_USER='repl', MASTER_PASSWORD='repl', MASTER_USE_GTID=Slave_pos",
                repl->IP[0],
                repl->port[0]);
        try_query(repl->nodes[1], "%s", sys1);
        try_query(repl->nodes[1], "start slave");
    }
    else
    {
        tprintf("show master status\n");
        find_field(repl->nodes[0], (char*) "show master status", (char*) "File", &log_file[0]);
        find_field(repl->nodes[0], (char*) "show master status", (char*) "Position", &log_pos[0]);
        tprintf("Real master file: %s\n", log_file);
        tprintf("Real master pos : %s\n", log_pos);

        tprintf("Stopping first slave (node 1)\n");
        try_query(repl->nodes[1], "stop slave;");
        // repl->no_set_pos = true;
        repl->no_set_pos = false;
        tprintf("Configure first backend slave node to be slave of real master\n");
        repl->set_slave(repl->nodes[1], repl->IP[0], repl->port[0], log_file, log_pos);
    }

    if (!local_maxscale)
    {
        tprintf("Starting back Maxscale\n");
        add_result(maxscales->start_maxscale(m), "Maxscale start failed\n");
    }
    else
    {
        perform_manual_action("Start Maxscale");
    }

    tprintf("Connecting to MaxScale binlog router (with any DB)\n");
    binlog =
        open_conn_no_db(maxscales->binlog_port[m], maxscales->IP[m], repl->user_name, repl->password, ssl);

    add_result(mysql_errno(binlog), "Error connection to binlog router %s\n", mysql_error(binlog));

    if (binlog_master_gtid)
    {
        // GTID to connect real Master
        tprintf("GTID for connection binlog router to master!\n");
        try_query(binlog, "stop slave");
        try_query(binlog, "SET @@global.gtid_slave_pos=''");
        sprintf(sys1,
                "CHANGE MASTER TO MASTER_HOST='%s', MASTER_PORT=%d, MASTER_USER='repl', MASTER_PASSWORD='repl', MASTER_USE_GTID=Slave_pos",
                repl->IP[0],
                repl->port[0]);
        try_query(binlog, "%s", sys1);
    }
    else
    {
        repl->no_set_pos = true;
        tprintf("configuring Maxscale binlog router\n");
        repl->set_slave(binlog, repl->IP[0], repl->port[0], log_file, log_pos);
    }
    // ssl between binlog router and Master
    if (backend_ssl)
    {
        sprintf(sys1,
                "CHANGE MASTER TO master_ssl_cert='%s/certs/client-cert.pem', master_ssl_ca='%s/certs/ca.pem', master_ssl=1, master_ssl_key='%s/certs/client-key.pem'",
                maxscales->access_homedir[m],
                maxscales->access_homedir[m],
                maxscales->access_homedir[m]);
        tprintf("Configuring Master ssl: %s\n", sys1);
        try_query(binlog, "%s", sys1);
    }
    try_query(binlog, "start slave");
    try_query(binlog, "show slave status");

    if (binlog_slave_gtid)
    {
        tprintf("GTID for connection slaves to binlog router!\n");
        tprintf("Setup all backend nodes except first one to be slaves of binlog Maxscale node\n");
        fflush(stdout);
        for (i = 2; i < repl->N; i++)
        {
            try_query(repl->nodes[i], "stop slave");
            try_query(repl->nodes[i], "SET @@global.gtid_slave_pos=''");
            sprintf(sys1,
                    "CHANGE MASTER TO MASTER_HOST='%s', MASTER_PORT=%d, MASTER_USER='repl', MASTER_PASSWORD='repl', MASTER_USE_GTID=Slave_pos",
                    maxscales->IP[m],
                    maxscales->binlog_port[m]);
            try_query(repl->nodes[i], "%s", sys1);
            try_query(repl->nodes[i], "start slave");
        }
    }
    else
    {
        repl->no_set_pos = false;

        // get Master status from Maxscale binlog
        tprintf("show master status\n");
        find_field(binlog, (char*) "show master status", (char*) "File", &log_file[0]);
        find_field(binlog, (char*) "show master status", (char*) "Position", &log_pos[0]);

        tprintf("Maxscale binlog master file: %s\n", log_file);
        tprintf("Maxscale binlog master pos : %s\n", log_pos);

        tprintf("Setup all backend nodes except first one to be slaves of binlog Maxscale node\n");
        fflush(stdout);
        for (i = 2; i < repl->N; i++)
        {
            try_query(repl->nodes[i], "stop slave");
            repl->set_slave(repl->nodes[i], maxscales->IP[m], maxscales->binlog_port[m], log_file, log_pos);
        }
    }

    repl->close_connections();
    try_query(binlog, "show slave status");
    mysql_close(binlog);
    repl->no_set_pos = no_pos;
    return global_result;
}

bool TestConnections::replicate_from_master(int m)
{
    bool rval = true;

    /** Stop the binlogrouter */
    MYSQL* conn = open_conn_no_db(maxscales->binlog_port[m],
                                  maxscales->IP[m],
                                  repl->user_name,
                                  repl->password,
                                  ssl);
    execute_query_silent(conn, "stop slave");
    mysql_close(conn);

    repl->execute_query_all_nodes("STOP SLAVE");

    /** Clean up MaxScale directories */
    maxscales->ssh_node(m, "service maxscale stop", true);
    prepare_binlog(m);
    maxscales->ssh_node(m, "service maxscale start", true);

    char log_file[256] = "";
    char log_pos[256] = "4";

    repl->connect();
    execute_query(repl->nodes[0], "RESET MASTER");

    conn = open_conn_no_db(maxscales->binlog_port[m], maxscales->IP[m], repl->user_name, repl->password, ssl);

    if (find_field(repl->nodes[0], "show master status", "File", log_file)
        || repl->set_slave(conn, repl->IP[0], repl->port[0], log_file, log_pos)
        || execute_query(conn, "start slave"))
    {
        rval = false;
    }

    mysql_close(conn);

    return rval;
}

void TestConnections::revert_replicate_from_master()
{
    char log_file[256] = "";

    repl->connect();
    execute_query(repl->nodes[0], "RESET MASTER");
    find_field(repl->nodes[0], "show master status", "File", log_file);

    for (int i = 1; i < repl->N; i++)
    {
        repl->set_slave(repl->nodes[i], repl->IP[0], repl->port[0], log_file, (char*)"4");
        execute_query(repl->nodes[i], "start slave");
    }
}

int TestConnections::start_mm(int m)
{
    int i;
    char log_file1[256];
    char log_pos1[256];
    char log_file2[256];
    char log_pos2[256];

    tprintf("Stopping maxscale\n");
    int global_result = maxscales->stop_maxscale(m);

    tprintf("Stopping all backend nodes\n");
    global_result += repl->stop_nodes();

    for (i = 0; i < 2; i++)
    {
        tprintf("Starting back node %d\n", i);
        global_result += repl->start_node(i, (char*) "");
    }

    repl->connect();
    for (i = 0; i < 2; i++)
    {
        execute_query(repl->nodes[i], "stop slave");
        execute_query(repl->nodes[i], "reset master");
    }

    execute_query(repl->nodes[0], "SET GLOBAL READ_ONLY=ON");

    find_field(repl->nodes[0], (char*) "show master status", (char*) "File", log_file1);
    find_field(repl->nodes[0], (char*) "show master status", (char*) "Position", log_pos1);

    find_field(repl->nodes[1], (char*) "show master status", (char*) "File", log_file2);
    find_field(repl->nodes[1], (char*) "show master status", (char*) "Position", log_pos2);

    repl->set_slave(repl->nodes[0], repl->IP[1], repl->port[1], log_file2, log_pos2);
    repl->set_slave(repl->nodes[1], repl->IP[0], repl->port[0], log_file1, log_pos1);

    repl->close_connections();

    tprintf("Starting back Maxscale\n");
    global_result += maxscales->start_maxscale(m);

    return global_result;
}

bool TestConnections::log_matches(int m, const char* pattern)
{
    return maxscales->ssh_node_f(m, true, "grep '%s' /var/log/maxscale/maxscale*.log", pattern) == 0;
}

void TestConnections::log_includes(int m, const char* pattern)
{
    add_result(!log_matches(m, pattern), "Log does not match pattern '%s'", pattern);
}

void TestConnections::log_excludes(int m, const char* pattern)
{
    add_result(log_matches(m, pattern), "Log matches pattern '%s'", pattern);
}

static int read_log(const char* name, char** err_log_content_p)
{
    FILE* f;
    *err_log_content_p = NULL;
    char* err_log_content;
    f = fopen(name, "rb");
    if (f != NULL)
    {

        int prev = ftell(f);
        fseek(f, 0L, SEEK_END);
        long int size = ftell(f);
        fseek(f, prev, SEEK_SET);
        err_log_content = (char*)malloc(size + 2);
        if (err_log_content != NULL)
        {
            fread(err_log_content, 1, size, f);
            for (int i = 0; i < size; i++)
            {
                if (err_log_content[i] == 0)
                {
                    // printf("null detected at position %d\n", i);
                    err_log_content[i] = '\n';
                }
            }
            // printf("s=%ld\n", strlen(err_log_content));
            err_log_content[size] = '\0';
            // printf("s=%ld\n", strlen(err_log_content));
            * err_log_content_p = err_log_content;
            return 0;
        }
        else
        {
            printf("Error allocationg memory for the log\n");
            return 1;
        }
    }
    else
    {
        printf ("Error reading log %s \n", name);
        return 1;
    }
}

void TestConnections::check_log_err(int m, const char* err_msg, bool expected)
{

    char* err_log_content;

    if (verbose)
    {
        tprintf("Getting logs");
    }
    char sys1[4096];
    char dest[1024];
    char log_file[64];
    set_timeout(500);
    sprintf(dest, "maxscale_log_%03d/", m);
    sprintf(&sys1[0],
            "mkdir -p maxscale_log_%03d; rm -f %s*.log",
            m,
            dest);

    system(sys1);
    sprintf(sys1, "%s/*", maxscales->maxscale_log_dir[m]);
    maxscales->copy_from_node(m, sys1, dest);

    if (verbose)
    {
        tprintf("Reading maxscale.log");
    }
    sprintf(log_file, "maxscale_log_%03d/maxscale.log", m);
    if (read_log(log_file, &err_log_content) != 0 || strlen(err_log_content) < 2)
    {
        if (verbose)
        {
            tprintf("Reading maxscale1.log");
        }
        sprintf(log_file, "maxscale_log_%03d/maxscale1.log", m);
        free(err_log_content);
        if (read_log(log_file, &err_log_content) != 0)
        {
            add_result(1, "Error reading log");
        }
    }

    if (err_log_content != NULL)
    {
        if (expected)
        {
            if (strstr(err_log_content, err_msg) == NULL)
            {
                add_result(1, "There is NO \"%s\" error in the log", err_msg);
            }
            else
            {
                tprintf("There is a proper \"%s \" error in the log", err_msg);
            }
        }
        else
        {
            if (strstr(err_log_content, err_msg) != NULL)
            {
                add_result(1, "There is an UNEXPECTED \"%s\" error in the log", err_msg);
            }
            else
            {
                tprintf("There are no unxpected \"%s \" errors in the log", err_msg);
            }
        }

        free(err_log_content);
    }
}

int TestConnections::find_connected_slave(int m, int* global_result)
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    repl->connect();
    for (int i = 0; i < repl->N; i++)
    {
        conn_num = get_conn_num(repl->nodes[i], maxscales->ip(m), maxscales->hostname[m], (char*) "test");
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

int TestConnections::find_connected_slave1(int m)
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    repl->connect();
    for (int i = 0; i < repl->N; i++)
    {
        conn_num = get_conn_num(repl->nodes[i], maxscales->ip(m), maxscales->hostname[m], (char*) "test");
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

int TestConnections::check_maxscale_processes(int m, int expected)
{
    int exit_code;
    char* maxscale_num = maxscales->ssh_node_output(m,
                                                    "ps -C maxscale | grep maxscale | wc -l",
                                                    false,
                                                    &exit_code);
    if ((maxscale_num == NULL) || (exit_code != 0))
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
        maxscale_num = maxscales->ssh_node_output(m,
                                                  "ps -C maxscale | grep maxscale | wc -l",
                                                  false,
                                                  &exit_code);
        if (atoi(maxscale_num) != expected)
        {
            add_result(1, "Number of MaxScale processes is not %d, it is %s\n", expected, maxscale_num);
        }
    }

    return exit_code;
}

int TestConnections::stop_maxscale(int m)
{
    int res = maxscales->ssh_node(m, "service maxscale stop", true);
    check_maxscale_processes(m, 0);
    fflush(stdout);
    return res;
}

int TestConnections::start_maxscale(int m)
{
    int res = maxscales->ssh_node(m, "service maxscale start", true);
    check_maxscale_processes(m, 1);
    fflush(stdout);
    return res;
}

int TestConnections::check_maxscale_alive(int m)
{
    int gr = global_result;
    set_timeout(10);
    tprintf("Connecting to Maxscale\n");
    add_result(maxscales->connect_maxscale(m), "Can not connect to Maxscale\n");
    tprintf("Trying simple query against all sevices\n");
    tprintf("RWSplit \n");
    set_timeout(10);
    try_query(maxscales->conn_rwsplit[m], "show databases;");
    tprintf("ReadConn Master \n");
    set_timeout(10);
    try_query(maxscales->conn_master[m], "show databases;");
    tprintf("ReadConn Slave \n");
    set_timeout(10);
    try_query(maxscales->conn_slave[m], "show databases;");
    set_timeout(10);
    maxscales->close_maxscale_connections(m);
    add_result(global_result - gr, "Maxscale is not alive\n");
    stop_timeout();

    check_maxscale_processes(m, 1);

    return global_result - gr;
}

int TestConnections::test_maxscale_connections(int m, bool rw_split, bool rc_master, bool rc_slave)
{
    int rval = 0;
    int rc;

    tprintf("Testing RWSplit, expecting %s\n", (rw_split ? "success" : "failure"));
    rc = execute_query(maxscales->conn_rwsplit[m], "select 1");
    if ((rc == 0) != rw_split)
    {
        tprintf("Error: Query %s\n", (rw_split ? "failed" : "succeeded"));
        rval++;
    }

    tprintf("Testing ReadConnRoute Master, expecting %s\n", (rc_master ? "success" : "failure"));
    rc = execute_query(maxscales->conn_master[m], "select 1");
    if ((rc == 0) != rc_master)
    {
        tprintf("Error: Query %s", (rc_master ? "failed" : "succeeded"));
        rval++;
    }

    tprintf("Testing ReadConnRoute Slave, expecting %s\n", (rc_slave ? "success" : "failure"));
    rc = execute_query(maxscales->conn_slave[m], "select 1");
    if ((rc == 0) != rc_slave)
    {
        tprintf("Error: Query %s", (rc_slave ? "failed" : "succeeded"));
        rval++;
    }
    return rval;
}


int TestConnections::create_connections(int m,
                                        int conn_N,
                                        bool rwsplit_flag,
                                        bool master_flag,
                                        bool slave_flag,
                                        bool galera_flag)
{
    int i;
    int local_result = 0;
    MYSQL* rwsplit_conn[conn_N];
    MYSQL* master_conn[conn_N];
    MYSQL* slave_conn[conn_N];
    MYSQL* galera_conn[conn_N];


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

            rwsplit_conn[i] = maxscales->open_rwsplit_connection(m);
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

            master_conn[i] = maxscales->open_readconn_master_connection(m);
            if (mysql_errno(master_conn[i]) != 0)
            {
                local_result++;
                tprintf("ReadConn master connection failed, error: %s\n", mysql_error(master_conn[i]));
            }
        }
        if (slave_flag)
        {
            if (verbose)
            {
                printf("ReadConn slave \t");
            }

            slave_conn[i] = maxscales->open_readconn_slave_connection(m);
            if (mysql_errno(slave_conn[i]) != 0)
            {
                local_result++;
                tprintf("ReadConn slave connection failed, error: %s\n", mysql_error(slave_conn[i]));
            }
        }
        if (galera_flag)
        {
            if (verbose)
            {
                printf("Galera \n");
            }

            galera_conn[i] =
                open_conn(4016, maxscales->IP[m], maxscales->user_name, maxscales->password, ssl);
            if (mysql_errno(galera_conn[i]) != 0)
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

    // global_result += check_pers_conn(Test, pers_conn_expected);
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

int TestConnections::get_client_ip(int m, char* ip)
{
    MYSQL* conn;
    MYSQL_RES* res;
    MYSQL_ROW row;
    int ret = 1;
    unsigned long long int rows;
    unsigned long long int i;

    maxscales->connect_rwsplit(m);
    if (execute_query(maxscales->conn_rwsplit[m],
                      "CREATE DATABASE IF NOT EXISTS db_to_check_client_ip") != 0)
    {
        return ret;
    }
    maxscales->close_rwsplit(m);
    conn = open_conn_db(maxscales->rwsplit_port[m],
                        maxscales->IP[m],
                        (char*) "db_to_check_client_ip",
                        maxscales->user_name,
                        maxscales->password,
                        ssl);

    if (conn != NULL)
    {
        if (mysql_query(conn, "show processlist;") != 0)
        {
            printf("Error: can't execute SQL-query: show processlist\n");
            printf("%s\n\n", mysql_error(conn));
        }
        else
        {
            res = mysql_store_result(conn);
            if (res == NULL)
            {
                printf("Error: can't get the result description\n");
            }
            else
            {
                mysql_num_fields(res);
                rows = mysql_num_rows(res);
                for (i = 0; i < rows; i++)
                {
                    row = mysql_fetch_row(res);
                    if ((row[2] != NULL ) && (row[3] != NULL))
                    {
                        if (strstr(row[3], "db_to_check_client_ip") != NULL)
                        {
                            ret = 0;
                            strcpy(ip, row[2]);
                        }
                    }
                }
            }
            mysql_free_result(res);
        }
        execute_query(maxscales->conn_rwsplit[m], "DROP DATABASE db_to_check_client_ip");
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

void TestConnections::tprintf(const char* format, ...)
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

int TestConnections::get_master_server_id(int m)
{
    int master_id = -1;
    MYSQL* conn = maxscales->open_rwsplit_connection(m);
    char str[100];
    if (find_field(conn, "SELECT @@server_id, @@last_insert_id;", "@@server_id", str) == 0)
    {
        char* endptr = NULL;
        auto colvalue = strtol(str, &endptr, 0);
        if (endptr && *endptr == '\0')
        {
            master_id = colvalue;
        }
    }
    mysql_close(conn);
    return master_id;
}
void* timeout_thread(void* ptr)
{
    TestConnections* Test = (TestConnections*) ptr;
    struct timespec tim;
    while (Test->timeout > 0)
    {
        tim.tv_sec = 1;
        tim.tv_nsec = 0;
        nanosleep(&tim, NULL);
        Test->timeout--;
    }
    Test->tprintf("\n **** Timeout! *** \n");
    Test->~TestConnections();
    exit(250);
}

void* log_copy_thread(void* ptr)
{
    TestConnections* Test = (TestConnections*) ptr;
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

    return NULL;
}

int TestConnections::insert_select(int m, int N)
{
    int result = 0;

    tprintf("Create t1\n");
    set_timeout(30);
    create_t1(maxscales->conn_rwsplit[m]);

    tprintf("Insert data into t1\n");
    set_timeout(N * 16 + 30);
    insert_into_t1(maxscales->conn_rwsplit[m], N);
    stop_timeout();
    repl->sync_slaves();

    tprintf("SELECT: rwsplitter\n");
    set_timeout(30);
    result += select_from_t1(maxscales->conn_rwsplit[m], N);

    tprintf("SELECT: master\n");
    set_timeout(30);
    result += select_from_t1(maxscales->conn_master[m], N);

    tprintf("SELECT: slave\n");
    set_timeout(30);
    result += select_from_t1(maxscales->conn_slave[m], N);

    return result;
}

int TestConnections::use_db(int m, char* db)
{
    int local_result = 0;
    char sql[100];

    sprintf(sql, "USE %s;", db);
    set_timeout(20);
    tprintf("selecting DB '%s' for rwsplit\n", db);
    local_result += execute_query(maxscales->conn_rwsplit[m], "%s", sql);
    tprintf("selecting DB '%s' for readconn master\n", db);
    local_result += execute_query(maxscales->conn_slave[m], "%s", sql);
    tprintf("selecting DB '%s' for readconn slave\n", db);
    local_result += execute_query(maxscales->conn_master[m], "%s", sql);
    for (int i = 0; i < repl->N; i++)
    {
        tprintf("selecting DB '%s' for direct connection to node %d\n", db, i);
        local_result += execute_query(repl->nodes[i], "%s", sql);
    }
    return local_result;
}

int TestConnections::check_t1_table(int m, bool presence, char* db)
{
    const char* expected = presence ? "" : "NOT";
    const char* actual = presence ? "NOT" : "";
    int start_result = global_result;

    add_result(use_db(m, db), "use db failed\n");
    stop_timeout();
    repl->sync_slaves();

    tprintf("Checking: table 't1' should %s be found in '%s' database\n", expected, db);
    set_timeout(30);
    int exists = check_if_t1_exists(maxscales->conn_rwsplit[m]);

    if (exists == presence)
    {
        tprintf("RWSplit: ok\n");
    }
    else
    {
        add_result(1, "Table t1 is %s found in '%s' database using RWSplit\n", actual, db);
    }

    set_timeout(30);
    exists = check_if_t1_exists(maxscales->conn_master[m]);

    if (exists == presence)
    {
        tprintf("ReadConn master: ok\n");
    }
    else
    {
        add_result(1,
                   "Table t1 is %s found in '%s' database using Readconnrouter with router option master\n",
                   actual,
                   db);
    }

    set_timeout(30);
    exists = check_if_t1_exists(maxscales->conn_slave[m]);

    if (exists == presence)
    {
        tprintf("ReadConn slave: ok\n");
    }
    else
    {
        add_result(1,
                   "Table t1 is %s found in '%s' database using Readconnrouter with router option slave\n",
                   actual,
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
            add_result(1,
                       "Table t1 is %s found in '%s' database using direct connect to node %d\n",
                       actual,
                       db,
                       i);
        }
    }

    stop_timeout();

    return global_result - start_result;
}

int TestConnections::try_query(MYSQL* conn, const char* format, ...)
{
    va_list valist;

    va_start(valist, format);
    int message_len = vsnprintf(NULL, 0, format, valist);
    va_end(valist);

    char sql[message_len + 1];

    va_start(valist, format);
    vsnprintf(sql, sizeof(sql), format, valist);
    va_end(valist);

    int res = execute_query_silent(conn, sql, false);
    add_result(res,
               "Query '%.*s%s' failed!\n",
               message_len < 100 ? message_len : 100,
               sql,
               message_len < 100 ? "" : "...");
    return res;
}

int TestConnections::try_query_all(int m, const char* sql)
{
    return try_query(maxscales->conn_rwsplit[m], "%s", sql)
           + try_query(maxscales->conn_master[m], "%s", sql)
           + try_query(maxscales->conn_slave[m], "%s", sql);
}

StringSet TestConnections::get_server_status(const char* name)
{
    std::set<std::string> rval;
    int rc;
    char* res = maxscales->ssh_node_output_f(0, true, &rc, "maxadmin list servers|grep \'%s\'", name);
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

int TestConnections::list_dirs(int m)
{
    for (int i = 0; i < repl->N; i++)
    {
        tprintf("ls on node %d\n", i);
        repl->ssh_node(i, (char*) "ls -la /var/lib/mysql", true);
        fflush(stdout);
    }
    tprintf("ls maxscale \n");
    maxscales->ssh_node(m, "ls -la /var/lib/maxscale/", true);
    fflush(stdout);
    return 0;
}

void TestConnections::check_current_operations(int m, int value)
{
    char value_str[512];
    sprintf(value_str, "%d", value);

    for (int i = 0; i < repl->N; i++)
    {
        char command[512];
        sprintf(command, "show server server%d", i + 1);
        add_result(maxscales->check_maxadmin_param(m, command, "Current no. of operations:", value_str),
                   "Current no. of operations is not %s",
                   value_str);
    }
}

void TestConnections::check_current_connections(int m, int value)
{
    char value_str[512];
    sprintf(value_str, "%d", value);

    for (int i = 0; i < repl->N; i++)
    {
        char command[512];
        sprintf(command, "show server server%d", i + 1);
        add_result(maxscales->check_maxadmin_param(m, command, "Current no. of conns:", value_str),
                   "Current no. of conns is not %s",
                   value_str);
    }
}

int TestConnections::take_snapshot(char* snapshot_name)
{
    char str[4096];
    sprintf(str, "%s %s", take_snapshot_command, snapshot_name);
    return system(str);
}

int TestConnections::revert_snapshot(char* snapshot_name)
{
    char str[4096];
    sprintf(str, "%s %s", revert_snapshot_command, snapshot_name);
    return system(str);
}

bool TestConnections::test_bad_config(int m, const char* config)
{
    process_template(m, config, "./");

    // Set the timeout to prevent hangs with configurations that work
    set_timeout(20);

    return maxscales->ssh_node_f(m,
                                 true,
                                 "cp maxscale.cnf /etc/maxscale.cnf; service maxscale stop; "
                                 "maxscale -U maxscale -lstdout &> /dev/null && sleep 1 && pkill -9 maxscale")
           == 0;
}

std::string dump_status(const StringSet& current, const StringSet& expected)
{
    std::stringstream ss;
    ss << "Current status: (";

    for (const auto& a : current)
    {
        ss << a << ",";
    }

    ss << ") Expected status: (";

    for (const auto& a : expected)
    {
        ss << a << ",";
    }

    ss << ")";

    return ss.str();
}

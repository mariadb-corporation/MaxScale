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
#include <string>
#include <fstream>
#include <iostream>
#include <future>
#include <limits.h>
#include <algorithm>

#include <maxbase/assert.h>
#include <maxbase/format.hh>
#include <maxbase/stacktrace.hh>
#include <maxbase/string.hh>

#include <maxtest/envv.hh>
#include <maxtest/galera_cluster.hh>
#include <maxtest/log.hh>
#include <maxtest/replication_cluster.hh>
#include <maxtest/mariadb_func.hh>
#include <maxtest/sql_t1.hh>
#include <maxtest/testconnections.hh>
#include <maxtest/test_info.hh>

using namespace mxb;
using std::cout;
using std::endl;
using std::string;

namespace
{
// These must match the labels recognized by MDBCI.
const string label_repl_be = "REPL_BACKEND";
const string label_galera_be = "GALERA_BACKEND";
const string label_big_be = "BIG_REPL_BACKEND";
const string label_2nd_mxs = "SECOND_MAXSCALE";
const string label_cs_be = "COLUMNSTORE_BACKEND";
const string label_clx_be = "XPAND_BACKEND";

const StringSet recognized_mdbci_labels =
{label_repl_be, label_big_be, label_galera_be, label_2nd_mxs, label_cs_be, label_clx_be};

const int MDBCI_FAIL = 200;     // Exit code when failure caused by MDBCI non-zero exit
const int BROKEN_VM_FAIL = 201; // Exit code when failure caused by broken VMs
const int TEST_SKIPPED = 202;   // Exit code when skipping test. Should match value expected by cmake.
}

namespace maxscale
{

static bool start = true;
static bool manual_debug = false;
static std::string required_repl_version;
static std::string required_galera_version;
static bool restart_galera = false;
static bool require_galera = false;
static bool require_columnstore = false;
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

static int call_system(const char* command)
{
    int rv = system(command);

    if (rv == -1)
    {
        printf("error: Could not execute '%s'.\n", command);
    }

    return rv;
}

void sigfatal_handler(int i)
{
    dump_stacktrace();
    signal_set(i, SIG_DFL);
    raise(i);
}

void TestConnections::skip_maxscale_start(bool value)
{
    maxscale::start = !value;
}

void TestConnections::require_repl_version(const char* version)
{
    maxscale::required_repl_version = version;
}

void TestConnections::require_galera_version(const char* version)
{
    maxscale::required_galera_version = version;
}

void TestConnections::require_galera(bool value)
{
    maxscale::require_galera = value;
}

void TestConnections::require_columnstore(bool value)
{
    maxscale::require_columnstore = value;
}

void TestConnections::restart_galera(bool value)
{
    maxscale::restart_galera = value;
}

TestConnections::TestConnections()
    : global_result(m_shared.log.m_n_fails)
{
}

TestConnections::TestConnections(int argc, char* argv[])
    : TestConnections()
{
    // These are required for backwards compatibility.
    m_shared.settings.req_mariadb_gtid = MariaDBCluster::get_require_gtid();

    int rc = prepare_for_test(argc, argv);
    if (rc != 0)
    {
        exit(rc);
    }
}

int TestConnections::prepare_for_test(int argc, char* argv[])
{
    std::ios::sync_with_stdio(true);
    set_signal_handlers();
    gettimeofday(&m_start_time, nullptr);

    // Read basic settings from env variables first, as cmdline may override.
    read_basic_settings();

    if (!read_cmdline_options(argc, argv))
    {
        return TEST_SKIPPED;
    }
    else if (maxscale::require_columnstore)
    {
        tprintf("ColumnStore testing is not yet implemented, skipping test");
        return TEST_SKIPPED;
    }
    else if (!read_test_info() || !check_create_vm_dir())
    {
        return 1;
    }

    bool vm_setup_ok = false;
    read_network_config();
    if (required_machines_are_running())
    {
        vm_setup_ok = true;
    }
    else
    {
        if (call_mdbci(""))
        {
            m_mdbci_called = true;
            // Network config should exist now.
            if (read_network_config())
            {
                if (required_machines_are_running())
                {
                    vm_setup_ok = true;
                }
                else
                {
                    add_failure("Still missing VMs after running MDBCI.");
                }
            }
            else
            {
                add_failure("Failed to read network_config or configured_labels after running MDBCI.");
            }
        }
    }

    if (!vm_setup_ok)
    {
        return MDBCI_FAIL;
    }

    bool node_error = !initialize_nodes();
    bool run_core_config = false;

    if (node_error || too_few_maxscales())
    {
        tprintf("Recreating VMs: %s", node_error ? "node check failed" : "too many maxscales");
        if (!call_mdbci("--recreate") || !initialize_nodes())
        {
            exit(MDBCI_FAIL);
        }
        run_core_config = true;

    }

    if (m_reinstall_maxscale)
    {
        if (reinstall_maxscales())
        {
            tprintf("Failed to install Maxscale: target is %s", m_target.c_str());
            exit(MDBCI_FAIL);
        }
        run_core_config = true;
    }

    if (run_core_config)
    {
        std::string src = std::string(test_dir) + "/mdbci/add_core_cnf.sh";
        maxscales->copy_to_node(0, src.c_str(), maxscales->access_homedir(0));
        maxscales->ssh_node_f(0, true, "%s/add_core_cnf.sh %s", maxscales->access_homedir(0),
                              verbose() ? "verbose" : "");
    }


    maxscales->set_use_ipv6(m_use_ipv6);
    maxscales->ssl = ssl;

    // Stop MaxScale to prevent it from interfering with the replication setup process
    if (!maxscale::manual_debug)
    {
        maxscales->stop_all();
    }

    if ((maxscale::restart_galera) && (galera))
    {
        galera->stop_nodes();
        galera->start_replication();
    }

    if (m_check_nodes)
    {
        if (repl && !repl->fix_replication())
        {
            exit(BROKEN_VM_FAIL);
        }
        if (galera && !galera->fix_replication())
        {
            exit(BROKEN_VM_FAIL);
        }
    }

    if (!check_backend_versions())
    {
        tprintf("Skipping test.");
        exit(TEST_SKIPPED);
    }

    if (m_init_maxscale)
    {
        init_maxscales();
    }

    if (m_mdbci_called)
    {
        auto res = maxscales->ssh_output("maxscale --version-full", 0, false);
        if (res.rc != 0)
        {
            tprintf("Error retrieving MaxScale version info");
        }
        else
        {
            tprintf("Maxscale_full_version_start:\n%s\nMaxscale_full_version_end\n", res.output.c_str());
        }
    }

    int rc = -1;
    string create_logdir_cmd = "mkdir -p LOGS/" + m_test_name;
    if (run_shell_command(create_logdir_cmd, "Failed to create logs directory."))
    {
        m_timeout_thread = std::thread(&TestConnections::timeout_thread, this);
        m_log_copy_thread = std::thread(&TestConnections::log_copy_thread, this);
        tprintf("Starting test");
        gettimeofday(&m_start_time, nullptr);
        logger().reset_timer();
        rc = 0;
    }

    return rc;
}

TestConnections::~TestConnections()
{
    if (!m_cleaned_up)
    {
        // Gets here if cleanup has not been explicitly called.
        int rc = cleanup();
        if (rc != 0)
        {
            exit(rc);
        }
        if (global_result)
        {
            // This causes the test to fail if a core dump is found
            exit(1);
        }
    }
    delete repl;
    delete galera;
    delete xpand;
    delete maxscales;
}

int TestConnections::cleanup()
{
    if (global_result > 0)
    {
        printf("\nTEST FAILURES:\n");
        printf("%s\n", logger().all_errors_to_string().c_str());
    }

    // Because cleanup is called even when system test init fails, we need to check various fields before
    // access.
    if (!settings().local_maxscale)
    {
        if (maxscales)
        {
            // stop all Maxscales to detect crashes on exit
            for (int i = 0; i < maxscales->N; i++)
            {
                stop_maxscale(i);
            }

            if (maxscales->use_valgrind())
            {
                sleep(15);      // sleep to let logs be written do disks
            }
        }
    }

    m_stop_threads = true;
    if (m_timeout_thread.joinable())
    {
        m_timeout_thread.join();
    }
    if (m_log_copy_thread.joinable())
    {
        m_log_copy_thread.join();
    }

    copy_all_logs();

    if (maxscales && settings().req_two_maxscales)
    {
        maxscales->stop_all();
    }
    m_cleaned_up = true;
    return 0;
}

void TestConnections::add_result(bool result, const char* format, ...)
{
    if (result)
    {
        va_list argp;
        va_start(argp, format);
        logger().add_failure_v(format, argp);
        va_end(argp);
    }
}

bool TestConnections::expect(bool result, const char* format, ...)
{
    va_list argp;
    va_start(argp, format);
    logger().expect_v(result, format, argp);
    va_end(argp);
    return result;
}

void TestConnections::add_failure(const char* format, ...)
{
    va_list argp;
    va_start(argp, format);
    logger().add_failure_v(format, argp);
    va_end(argp);
}

/**
 * Read the contents of both the 'network_config' and 'configured_labels'-files. The files may not exist
 * if the VM setup has not yet been initialized.
 *
 * @return True on success
 */
bool TestConnections::read_network_config()
{
    m_network_config.clear();
    m_configured_mdbci_labels.clear();
    const char warnmsg_fmt[] = "Warning: Failed to open '%s'. File needs to be created.";
    bool rval = false;

    string nwconf_filepath = m_vm_path + "_network_config";
    std::ifstream nwconf_file(nwconf_filepath);
    if (nwconf_file.is_open())
    {
        string line;
        while (std::getline(nwconf_file, line))
        {
            if (!line.empty())
            {
                // The line should be of form <key> = <value>.
                auto eq_pos = line.find('=');
                if (eq_pos != string::npos && eq_pos > 0 && eq_pos < line.length() - 1)
                {
                    string key = line.substr(0, eq_pos);
                    string val = line.substr(eq_pos + 1, string::npos);
                    mxb::trim(key);
                    mxb::trim(val);
                    if (!key.empty() && !val.empty())
                    {
                        m_network_config.insert(std::make_pair(key, val));
                    }
                }
            }
        }

        string labels_filepath = m_vm_path + "_configured_labels";
        std::ifstream labels_file(labels_filepath);
        if (labels_file.is_open())
        {
            // The file should contain just one line.
            line.clear();
            std::getline(labels_file, line);
            m_configured_mdbci_labels = parse_to_stringset(line);
            if (!m_configured_mdbci_labels.empty())
            {
                rval = true;
            }
            else
            {
                tprintf("Warning: Could not read any labels from '%s'", labels_filepath.c_str());
            }
        }
        else
        {
            tprintf(warnmsg_fmt, labels_filepath.c_str());
        }
    }
    else
    {
        tprintf(warnmsg_fmt, nwconf_filepath.c_str());
    }
    return rval;
}

void TestConnections::read_basic_settings()
{
    // The following settings can be overridden by cmdline settings, but not by mdbci.
    ssl = readenv_bool("ssl", true);
    m_use_ipv6 = readenv_bool("use_ipv6", false);
    backend_ssl = readenv_bool("backend_ssl", false);
    smoke = readenv_bool("smoke", true);
    m_threads = readenv_int("threads", 4);
    m_no_maxscale_log_copy = readenv_bool("no_maxscale_log_copy", false);

    if (readenv_bool("no_nodes_check", false))
    {
        m_check_nodes = false;
    }

    if (readenv_bool("no_maxscale_start", false))
    {
        maxscale::start = false;
    }

    // The following settings are final, and not modified by either command line parameters or mdbci.
    m_no_backend_log_copy = readenv_bool("no_backend_log_copy", false);
    m_mdbci_vm_path = envvar_get_set("MDBCI_VM_PATH", "%s/vms/", getenv("HOME"));
    m_mdbci_config_name = envvar_get_set("mdbci_config_name", "local");
    mxb_assert(!m_mdbci_vm_path.empty() && !m_mdbci_config_name.empty());
    m_vm_path = m_mdbci_vm_path + "/" + m_mdbci_config_name;

    m_mdbci_template = envvar_get_set("template", "default");
    m_target = envvar_get_set("target", "develop");
}

/**
 * Using the test name as given on the cmdline, get test config file and labels.
 */
bool TestConnections::read_test_info()
{
    const TestDefinition* found = nullptr;
    for (int i = 0; test_definitions[i].name; i++)
    {
        auto* test = &test_definitions[i];
        if (test->name == m_test_name)
        {
            found = test;
            break;
        }
    }

    if (found)
    {
        m_cnf_template_path = found->config_template;
        // Parse the labels-string to a set.
        auto test_labels = parse_to_stringset(found->labels);

        /**
         * MDBCI recognizes labels which affect backend configuration. Save those labels to a separate field.
         * Also save a string version, as that is needed for mdbci.
         */
        StringSet mdbci_labels;
        mdbci_labels.insert("MAXSCALE");
        std::set_intersection(test_labels.begin(), test_labels.end(),
                              recognized_mdbci_labels.begin(), recognized_mdbci_labels.end(),
                              std::inserter(mdbci_labels, mdbci_labels.begin()));

        m_required_mdbci_labels = mdbci_labels;
        m_required_mdbci_labels_str = flatten_stringset(mdbci_labels);

        tprintf("Test: '%s', MaxScale config file: '%s', all labels: '%s', mdbci labels: '%s'",
                m_test_name.c_str(), m_cnf_template_path.c_str(), found->labels,
                m_required_mdbci_labels_str.c_str());

        if (test_labels.count("BACKEND_SSL") > 0)
        {
            backend_ssl = true;
        }
    }
    else
    {
        add_failure("Could not find '%s' in the CMake-generated test definitions array.",
                    m_test_name.c_str());
    }
    return found != nullptr;
}

/**
 * Process a MaxScale configuration file. Replaces the placeholders in the text with correct values.
 *
 * @param config_file_path Config file template path
 * @param dest Destination file name for actual configuration file
 * @return True on success
 */
bool TestConnections::process_template(int m, const string& config_file_path, const char* dest)
{
    tprintf("Processing MaxScale config file %s\n", config_file_path.c_str());
    std::ifstream config_file(config_file_path);
    string file_contents;
    if (config_file.is_open())
    {
        file_contents.reserve(10000);
        file_contents.assign((std::istreambuf_iterator<char>(config_file)),
                             (std::istreambuf_iterator<char>()));
    }

    if (file_contents.empty())
    {
        int eno = errno;
        add_failure("Failed to read MaxScale config file template '%s' or file was empty. Error %i: %s",
                    config_file_path.c_str(), eno, mxb_strerror(eno));
        return false;
    }

    // Replace various items in the config file text, then write it to disk. Define a helper function.
    auto replace_text = [&file_contents](const string& what, const string& replacement) {
            bool found = true;
            while (found)
            {
                auto pos = file_contents.find(what);
                if (pos != string::npos)
                {
                    file_contents.replace(pos, what.length(), replacement);
                }
                else
                {
                    found = false;
                }
            }
        };

    replace_text("###threads###", std::to_string(m_threads));

    MariaDBCluster* clusters[] = {repl, galera, xpand};
    for (auto cluster : clusters)
    {
        if (cluster)
        {
            bool using_ip6 = cluster->using_ipv6();
            auto& prefix = cluster->prefix();

            for (int i = 0; i < cluster->N; i++)
            {
                // The placeholders in the config template use the node name prefix, not the MaxScale config
                // server name prefix.
                string ip_ph = mxb::string_printf("###%s_server_IP_%0d###", prefix.c_str(), i + 1);
                string ip_str = using_ip6 ? cluster->ip6(i) : cluster->ip_private(i);
                replace_text(ip_ph, ip_str);

                string port_ph = mxb::string_printf("###%s_server_port_%0d###", prefix.c_str(), i + 1);
                string port_str = std::to_string(cluster->port[i]);
                replace_text(port_ph, port_str);
            }

            // The following generates basic server definitions for all servers.
            string all_servers_ph = mxb::string_printf("###%s###", cluster->cnf_srv_name().c_str());
            string all_servers_str = cluster->cnf_servers();
            replace_text(all_servers_ph, all_servers_str);

            // The following generates one line with server names. Used with monitors and services.
            string all_servers_line_ph = mxb::string_printf("###%s_line###", cluster->cnf_srv_name().c_str());
            string all_server_line_str = cluster->cnf_servers_line();
            replace_text(all_servers_line_ph, all_server_line_str);
        }
    }

    bool rval = false;
    // Do the remaining replacements with sed, for now.
    const string target_file = "maxscale.cnf";
    std::ofstream output_file(target_file);
    if (output_file.is_open())
    {
        output_file << file_contents;
        output_file.close();

        const string edit_failed = "Config file edit failed";
        if (backend_ssl)
        {
            tprintf("Adding ssl settings\n");
            const string sed_cmd = "sed -i "
                                   "\"s|type *= *server|type=server\\nssl=true\\nssl_cert=/###access_homedir###/"
                                   "certs/client-cert.pem\\nssl_key=/###access_homedir###/certs/client-key.pem"
                                   "\\nssl_ca_cert=/###access_homedir###/certs/ca.pem\\nssl_cert_verify_depth=9"
                                   "\\nssl_version=MAX|g\" maxscale.cnf";
            run_shell_command(sed_cmd, edit_failed);
        }

        string sed_cmd = mxb::string_printf("sed -i \"s/###access_user###/%s/g\" %s",
                                            maxscales->access_user(m), target_file.c_str());
        run_shell_command(sed_cmd, edit_failed);

        sed_cmd = mxb::string_printf("sed -i \"s|###access_homedir###|%s|g\" %s",
                                     maxscales->access_homedir(m), target_file.c_str());
        run_shell_command(sed_cmd, edit_failed);

        maxscales->copy_to_node(m, "maxscale.cnf", dest);
        rval = true;
    }
    else
    {
        int eno = errno;
        add_failure("Could not write to '%s'. Error %i, %s", target_file.c_str(), eno, mxb_strerror(eno));
    }
    return rval;
}

void TestConnections::init_maxscales()
{
    // Always initialize the first MaxScale
    init_maxscale(0);

    if (settings().req_two_maxscales)
    {
        for (int i = 1; i < maxscales->N; i++)
        {
            init_maxscale(i);
        }
    }
}

void TestConnections::init_maxscale(int m)
{
    auto homedir = maxscales->access_homedir(m);
    // The config file path can be multivalued when running a test with multiple MaxScales.
    // Select the correct file.
    auto filepaths = mxb::strtok(m_cnf_template_path, ";");
    int n_files = filepaths.size();
    if (m < n_files)
    {
        // Have a separate config file for this MaxScale.
        process_template(m, filepaths[m], homedir);
    }
    else if (n_files >= 1)
    {
        // Not enough config files given for all MaxScales. Use the config of first MaxScale. This can
        // happen with the "check_backends"-test.
        tprintf("MaxScale %i does not have a designated config file, only found %i files in test definition. "
                "Using main MaxScale config file instead.", m, n_files);
        process_template(m, filepaths[0], homedir);
    }
    else
    {
        tprintf("No MaxScale config files defined. MaxScale may not start.");
    }

    // TODO: Do this somewhere better.
    auto create_test_db = [](MariaDBCluster* cluster) {
            if (cluster)
            {
                cluster->connect();
                execute_query(cluster->nodes[0], "CREATE DATABASE IF NOT EXISTS test;");
                cluster->close_connections();
            }
        };
    create_test_db(repl);
    create_test_db(galera);
    create_test_db(xpand);

    if (maxscales->ssh_node_f(m, true, "test -d %s/certs", homedir))
    {
        tprintf("SSL certificates not found, copying to maxscale");
        maxscales->ssh_node_f(m, true, "rm -rf %s/certs;mkdir -m a+wrx %s/certs;", homedir, homedir);

        char str[4096];
        char dtr[4096];
        sprintf(str, "%s/ssl-cert/*", test_dir);
        sprintf(dtr, "%s/certs/", homedir);
        maxscales->copy_to_node_legacy(str, dtr, m);
        sprintf(str, "cp %s/ssl-cert/* .", test_dir);
        call_system(str);
        maxscales->ssh_node_f(m, true, "chmod -R a+rx %s;", homedir);
    }

    maxscales->ssh_node_f(m,
                          true,
                          "cp maxscale.cnf %s;"
                          "iptables -F INPUT;"
                          "rm -rf %s/*.log /tmp/core* /dev/shm/* /var/lib/maxscale/* /var/lib/maxscale/.secrets;"
                          "find /var/*/maxscale -name 'maxscale.lock' -delete;",
                          maxscales->maxscale_cnf[m].c_str(),
                          maxscales->maxscale_log_dir[m].c_str());
    if (maxscale::start)
    {
        maxscales->restart_maxscale(m);
        maxscales->ssh_node_f(m,
                              true,
                              "maxctrl api get maxscale/debug/monitor_wait");
    }
    else
    {
        maxscales->stop_maxscale(m);
    }
}

void TestConnections::copy_one_mariadb_log(MariaDBCluster* nrepl, int i, std::string filename)
{
    auto log_retrive_commands =
    {
        "cat /var/lib/mysql/*.err",
        "cat /var/log/syslog | grep mysql",
        "cat /var/log/messages | grep mysql"
    };

    int j = 1;

    for (auto cmd : log_retrive_commands)
    {
        auto output = nrepl->ssh_output(cmd, i).output;

        if (!output.empty())
        {
            std::ofstream outfile(filename + std::to_string(j++));

            if (outfile)
            {
                outfile << output;
            }
        }
    }
}

int TestConnections::copy_mariadb_logs(MariaDBCluster* nrepl,
                                       const char* prefix,
                                       std::vector<std::thread>& threads)
{
    int local_result = 0;

    if (nrepl)
    {
        for (int i = 0; i < nrepl->N; i++)
        {
            // Do not copy MariaDB logs in case of local backend
            if (strcmp(nrepl->ip4(i), "127.0.0.1") != 0)
            {
                char str[4096];
                sprintf(str, "LOGS/%s/%s%d_mariadb_log", m_test_name.c_str(), prefix, i);
                threads.emplace_back(&TestConnections::copy_one_mariadb_log, this, nrepl, i, string(str));
            }
        }
    }

    return local_result;
}

int TestConnections::copy_all_logs()
{
    set_timeout(300);

    char str[PATH_MAX + 1];
    sprintf(str, "mkdir -p LOGS/%s", m_test_name.c_str());
    call_system(str);

    std::vector<std::thread> threads;

    if (!m_no_backend_log_copy)
    {
        copy_mariadb_logs(repl, "node", threads);
        copy_mariadb_logs(galera, "galera", threads);
    }

    int rv = 0;

    if (!m_no_maxscale_log_copy)
    {
        rv = copy_maxscale_logs(0);
    }

    for (auto& a : threads)
    {
        a.join();
    }

    return rv;
}

void TestConnections::copy_one_maxscale_log(int i, double timestamp)
{
    char log_dir[PATH_MAX + 1024];
    char log_dir_i[sizeof(log_dir) + 1024];
    char sys[sizeof(log_dir_i) + 1024];
    if (timestamp == 0)
    {
        sprintf(log_dir, "LOGS/%s", m_test_name.c_str());
    }
    else
    {
        sprintf(log_dir, "LOGS/%s/%04f", m_test_name.c_str(), timestamp);
    }

    sprintf(log_dir_i, "%s/%03d", log_dir, i);
    sprintf(sys, "mkdir -p %s", log_dir_i);
    call_system(sys);

    if (strcmp(maxscales->ip4(i), "127.0.0.1") != 0)
    {
        auto homedir = maxscales->access_homedir(i);
        int rc = maxscales->ssh_node_f(i, true,
                                       "rm -rf %s/logs;"
                                       "mkdir %s/logs;"
                                       "cp %s/*.log %s/logs/;"
                                       "test -e /tmp/core* && cp /tmp/core* %s/logs/ >& /dev/null;"
                                       "cp %s %s/logs/;"
                                       "chmod 777 -R %s/logs;"
                                       "test -e /tmp/core*  && exit 42;",
                                       homedir,
                                       homedir,
                                       maxscales->maxscale_log_dir[i].c_str(), homedir,
                                       homedir,
                                       maxscales->maxscale_cnf[i].c_str(), homedir,
                                       homedir);
        sprintf(sys, "%s/logs/*", homedir);
        maxscales->copy_from_node(i, sys, log_dir_i);
        expect(rc != 42, "Test should not generate core files");
    }
    else
    {
        maxscales->ssh_node_f(i, true, "cp %s/*.logs %s/", maxscales->maxscale_log_dir[i].c_str(), log_dir_i);
        maxscales->ssh_node_f(i, true, "cp /tmp/core* %s/", log_dir_i);
        maxscales->ssh_node_f(i, true, "cp %s %s/", maxscales->maxscale_cnf[i].c_str(), log_dir_i);
        maxscales->ssh_node_f(i, true, "chmod a+r -R %s", log_dir_i);
    }
}

int TestConnections::copy_maxscale_logs(double timestamp)
{
    std::vector<std::thread> threads;

    for (int i = 0; i < maxscales->N; i++)
    {
        threads.emplace_back([this, i, timestamp]() {
                                 copy_one_maxscale_log(i, timestamp);
                             });
    }

    for (auto& a : threads)
    {
        a.join();
    }

    return 0;
}

int TestConnections::copy_all_logs_periodic()
{
    timeval t2;
    gettimeofday(&t2, NULL);
    double elapsedTime = (t2.tv_sec - m_start_time.tv_sec);
    elapsedTime += (double) (t2.tv_usec - m_start_time.tv_usec) / 1000000.0;

    return copy_maxscale_logs(elapsedTime);
}

void TestConnections::revert_replicate_from_master()
{
    char log_file[256] = "";

    repl->connect();
    execute_query(repl->nodes[0], "RESET MASTER");
    find_field(repl->nodes[0], "show master status", "File", log_file);

    for (int i = 1; i < repl->N; i++)
    {
        repl->set_slave(repl->nodes[i], repl->ip_private(0), repl->port[0], log_file, (char*)"4");
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

    repl->set_slave(repl->nodes[0], repl->ip_private(1), repl->port[1], log_file2, log_pos2);
    repl->set_slave(repl->nodes[1], repl->ip_private(0), repl->port[0], log_file1, log_pos1);

    repl->close_connections();

    tprintf("Starting back Maxscale\n");
    global_result += maxscales->start_maxscale(m);

    return global_result;
}

bool TestConnections::log_matches(int m, const char* pattern)
{

    // Replace single quotes with wildcard characters, should solve most problems
    std::string p = pattern;
    for (auto& a : p)
    {
        if (a == '\'')
        {
            a = '.';
        }
    }

    return maxscales->ssh_node_f(m, true, "grep '%s' /var/log/maxscale/maxscale*.log", p.c_str()) == 0;
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
            fclose(f);
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

int TestConnections::find_connected_slave1(int m)
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    repl->connect();
    for (int i = 0; i < repl->N; i++)
    {
        conn_num = get_conn_num(repl->nodes[i], maxscales->ip(m), maxscales->hostname(m), (char*) "test");
        tprintf("connections to %d: %u\n", i, conn_num);
        all_conn += conn_num;
        if ((i != 0) && (conn_num != 0))
        {
            current_slave = i;
        }
    }
    tprintf("Now connected slave node is %d (%s)\n", current_slave, repl->ip4(current_slave));
    repl->close_connections();
    return current_slave;
}

int TestConnections::check_maxscale_processes(int m, int expected)
{
    const char* ps_cmd = maxscales->use_valgrind() ?
        "ps ax | grep valgrind | grep maxscale | grep -v grep | wc -l" :
        "ps -C maxscale | grep maxscale | wc -l";

    auto maxscale_num = maxscales->ssh_output(ps_cmd, m, false);
    if (maxscale_num.output.empty() || (maxscale_num.rc != 0))
    {
        return -1;
    }

    maxscale_num.output = cutoff_string(maxscale_num.output, '\n');

    if (atoi(maxscale_num.output.c_str()) != expected)
    {
        tprintf("%s maxscale processes detected, trying again in 5 seconds\n", maxscale_num.output.c_str());
        sleep(5);
        maxscale_num = maxscales->ssh_output(ps_cmd, m, false);

        if (atoi(maxscale_num.output.c_str()) != expected)
        {
            add_result(1, "Number of MaxScale processes is not %d, it is %s\n",
                       expected, maxscale_num.output.c_str());
        }
    }

    return maxscale_num.rc;
}

int TestConnections::stop_maxscale(int m)
{
    int res = maxscales->stop_maxscale(m);
    check_maxscale_processes(m, 0);
    fflush(stdout);
    return res;
}

int TestConnections::start_maxscale(int m)
{
    int res = maxscales->start_maxscale(m);
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
    const bool verbose = this->verbose();

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
                open_conn(4016, maxscales->ip4(m), maxscales->user_name, maxscales->password, ssl);
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

int TestConnections::set_timeout(long int timeout_seconds)
{
    if (m_enable_timeouts)
    {
        m_timeout = timeout_seconds;
    }
    return 0;
}

void TestConnections::set_test_timeout(std::chrono::seconds timeout)
{
    m_test_timeout = timeout;
}

int TestConnections::set_log_copy_interval(long int interval_seconds)
{
    m_log_copy_to_go = interval_seconds;
    m_log_copy_interval = interval_seconds;
    return 0;
}

int TestConnections::stop_timeout()
{
    m_timeout = 999999999;
    return 0;
}

void TestConnections::tprintf(const char* format, ...)
{
    va_list argp;
    va_start(argp, format);
    logger().log_msg(format, argp);
    va_end(argp);
}

void TestConnections::log_printf(const char* format, ...)
{
    va_list argp;
    va_start(argp, format);
    int n = vsnprintf(nullptr, 0, format, argp);
    va_end(argp);

    va_start(argp, format);
    char buf[n + 1];
    vsnprintf(buf, sizeof(buf), format, argp);
    va_end(argp);

    tprintf("%s", buf);

    while (char* c = strchr(buf, '\''))
    {
        *c = '^';
    }

    maxscales->ssh_node_f(0, true, "echo '--- %s ---' >> /var/log/maxscale/maxscale.log", buf);
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

void TestConnections::timeout_thread()
{
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();

    while (!m_stop_threads && m_timeout > 0 && Clock::now() - start < m_test_timeout)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        m_timeout--;
    }

    if (!m_stop_threads)
    {
        copy_all_logs();
        tprintf("\n **** Timeout! *** \n");
        exit(250);
    }
}

void TestConnections::log_copy_thread()
{
    while (!m_stop_threads)
    {
        while (!m_stop_threads && m_log_copy_to_go > 0)
        {
            struct timespec tim;
            tim.tv_sec = 1;
            tim.tv_nsec = 0;
            nanosleep(&tim, NULL);
            m_log_copy_to_go--;
        }

        if (!m_stop_threads)
        {
            m_log_copy_to_go = m_log_copy_interval;
            tprintf("\n **** Copying all logs *** \n");
            copy_all_logs_periodic();
        }
    }
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

StringSet TestConnections::get_server_status(const std::string& name, int m)
{
    return maxscales->get_server_status(name, m);
}

void TestConnections::check_current_operations(int m, int value)
{
    for (int i = 0; i < repl->N; i++)
    {
        auto res = maxctrl("api get servers/server"
                           + std::to_string(i + 1)
                           + " data.attributes.statistics.active_operations", m);

        expect(std::stoi(res.output) == value,
               "Current no. of operations is not %d for server%d", value, i + 1);
    }
}

void TestConnections::check_current_connections(int m, int value)
{
    for (int i = 0; i < repl->N; i++)
    {
        auto res = maxctrl("api get servers/server"
                           + std::to_string(i + 1)
                           + " data.attributes.statistics.connections", m);

        expect(std::stoi(res.output) == value,
               "Current no. of conns is not %d for server%d", value, i + 1);
    }
}

void TestConnections::check_current_persistent_connections(int m, const std::string& name, int value)
{
    auto res = maxctrl("api get servers/" + name
                       + " data.attributes.statistics.persistent_connections", m);

    expect(atoi(res.output.c_str()) == value,
           "Current no. of persistent conns is '%s' not '%d' for %s",
           res.output.c_str(), value, name.c_str());
}

bool TestConnections::test_bad_config(int m, const string& config)
{
    process_template(m, config, "/tmp/");

    // Set the timeout to prevent hangs with configurations that work
    set_timeout(20);

    int ssh_rc = maxscales->ssh_node_f(m,
                                 true,
                                 "cp /tmp/maxscale.cnf /etc/maxscale.cnf; pkill -9 maxscale; "
                                 "maxscale -U maxscale -lstdout &> /dev/null && sleep 1 && pkill -9 maxscale");
    return ((ssh_rc == 0) || (ssh_rc == 256));
}

/**
 * Run MDBCI to bring up nodes.
 *
 * @return True on success
 */
bool TestConnections::call_mdbci(const char* options)
{
    if (access(m_vm_path.c_str(), F_OK) != 0)
    {
        // Directory does not exist, must be first time running mdbci.
        bool ok = false;
        if (process_mdbci_template())
        {
            string mdbci_gen_cmd = mxb::string_printf("mdbci --override --template %s.json generate %s",
                                                      m_vm_path.c_str(), m_mdbci_config_name.c_str());
            if (run_shell_command(mdbci_gen_cmd, "MDBCI failed to generate virtual machines description"))
            {
                string copy_cmd = mxb::string_printf("cp -r %s/mdbci/cnf %s/", test_dir, m_vm_path.c_str());
                if (run_shell_command(copy_cmd, "Failed to copy my.cnf files"))
                {
                    ok = true;
                }
            }
        }

        if (!ok)
        {
            return false;
        }
    }

    bool rval = false;
    string mdbci_up_cmd = mxb::string_printf("mdbci up %s --labels %s %s",
                                             m_mdbci_config_name.c_str(), m_required_mdbci_labels_str.c_str(),
                                             options);
    if (run_shell_command(mdbci_up_cmd, "MDBCI failed to bring up virtual machines"))
    {
        std::string team_keys = envvar_get_set("team_keys", "~/.ssh/id_rsa.pub");
        string keys_cmd = mxb::string_printf("mdbci public_keys --key %s %s",
                                             team_keys.c_str(), m_mdbci_config_name.c_str());
        if (run_shell_command(keys_cmd, "MDBCI failed to upload ssh keys."))
        {
            rval = true;
        }
    }

    return rval;
}

/**
 * Read template file from maxscale-system-test/mdbci/templates and replace all placeholders with
 * actual values.
 *
 * @return True on success
 */
bool TestConnections::process_mdbci_template()
{
    string box = envvar_get_set("box", "centos_7_libvirt");
    string backend_box = envvar_get_set("backend_box", "%s", box.c_str());
    envvar_get_set("xpand_box", "%s", backend_box.c_str());
    envvar_get_set("vm_memory", "2048");
    envvar_get_set("maxscale_product", "maxscale_ci");
    envvar_get_set("force_maxscale_version", "true");
    envvar_get_set("force_backend_version", "true");

    string version = envvar_get_set("version", "10.5");
    envvar_get_set("galera_version", "%s", version.c_str());

    string product = envvar_get_set("product", "mariadb");
    string cnf_path;
    if (product == "mysql")
    {
        cnf_path = mxb::string_printf("%s/cnf/mysql56/", m_vm_path.c_str());
    }
    else
    {
        cnf_path = mxb::string_printf("%s/cnf/", m_vm_path.c_str());
    }
    setenv("cnf_path", cnf_path.c_str(), 1);

    string template_file = mxb::string_printf("%s/mdbci/templates/%s.json.template",
                                              test_dir, m_mdbci_template.c_str());
    string target_file = m_vm_path + ".json";
    string subst_cmd = "envsubst < " + template_file + " > " + target_file;

    bool rval = false;
    if (run_shell_command(subst_cmd, "Failed to generate VM json config file."))
    {
        if (verbose())
        {
            tprintf("Generated VM json config file with '%s'.", subst_cmd.c_str());
        }

        string mdbci_gen_cmd = mxb::string_printf("mdbci --override --template %s generate %s",
                                                  target_file.c_str(), m_mdbci_config_name.c_str());
        if (run_shell_command(mdbci_gen_cmd, "MDBCI failed to generate VM configuration."))
        {
            rval = true;
            if (verbose())
            {
                tprintf("Generated VM configuration with '%s'.", subst_cmd.c_str());
            }
        }
    }
    return rval;
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

int TestConnections::reinstall_maxscales()
{
    for (int i = 0; i < maxscales->N; i++)
    {
        printf("Installing Maxscale on node %d\n", i);
        // TODO: make it via MDBCI and compatible with any distro
        maxscales->ssh_node(i, "yum remove maxscale -y", true);
        maxscales->ssh_node(i, "yum clean all", true);

        string install_cmd = mxb::string_printf(
            "mdbci install_product --product maxscale_ci --product-version %s %s/%s_%03d",
            m_target.c_str(), m_mdbci_config_name.c_str(), maxscales->prefix().c_str(), i);
        if (!run_shell_command(install_cmd, "MaxScale install failed."))
        {
            return 1;
        }
    }
    return 0;
}

bool TestConnections::too_few_maxscales() const
{
    return maxscales->N < 2 && m_required_mdbci_labels.count(label_2nd_mxs) > 0;
}

std::string TestConnections::flatten_stringset(const StringSet& set)
{
    string rval;
    string sep;
    for (auto& elem : set)
    {
        rval += sep;
        rval += elem;
        sep = ",";
    }
    return rval;
}

StringSet TestConnections::parse_to_stringset(const string& source)
{
    string copy = source;
    StringSet rval;
    if (!copy.empty())
    {
        char* ptr = &copy[0];
        char* save_ptr = nullptr;
        // mdbci uses ',' and cmake uses ';'. Add ' ' and newline as well to ensure trimming.
        const char delim[] = ",; \n";
        char* token = strtok_r(ptr, delim, &save_ptr);
        while (token)
        {
            rval.insert(token);
            token = strtok_r(nullptr, delim, &save_ptr);
        }
    }
    return rval;
}

mxt::MaxScale& TestConnections::maxscale()
{
    return *m_maxscale;
}

mxt::MaxScale& TestConnections::maxscale2()
{
    return *m_maxscale2;
}

mxt::TestLogger& TestConnections::logger()
{
    return m_shared.log;
}

mxt::Settings& TestConnections::settings()
{
    return m_shared.settings;
}

bool TestConnections::read_cmdline_options(int argc, char* argv[])
{
    static struct option long_options[] =
    {
        {"help",               no_argument,       0, 'h'},
        {"verbose",            no_argument,       0, 'v'},
        {"silent",             no_argument,       0, 'n'},
        {"quiet",              no_argument,       0, 'q'},
        {"no-maxscale-start",  no_argument,       0, 's'},
        {"no-maxscale-init",   no_argument,       0, 'i'},
        {"no-nodes-check",     no_argument,       0, 'r'},
        {"restart-galera",     no_argument,       0, 'g'},
        {"no-timeouts",        no_argument,       0, 'z'},
        {"local-maxscale",     optional_argument, 0, 'l'},
        {"reinstall-maxscale", no_argument,       0, 'm'},
        {0,                    0,                 0, 0  }
    };

    bool rval = true;
    int c;
    int option_index = 0;

    while ((c = getopt_long(argc, argv, "hvnqsirgzlm::", long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case 'v':
            set_verbose(true);
            break;

        case 'n':
            set_verbose(false);
            break;

        case 'q':
            if (!freopen("/dev/null", "w", stdout))
            {
                printf("warning: Could not redirect stdout to /dev/null.\n");
            }
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
                rval = false;
            }
            break;

        case 's':
            printf("Maxscale won't be started\n");
            maxscale::start = false;
            maxscale::manual_debug = true;
            break;

        case 'i':
            printf("Maxscale won't be started and Maxscale.cnf won't be uploaded\n");
            m_init_maxscale = false;
            break;

        case 'r':
            printf("Nodes are not checked before test and are not restarted\n");
            m_check_nodes = false;
            break;

        case 'g':
            printf("Restarting Galera setup\n");
            maxscale::restart_galera = true;
            break;

        case 'z':
            m_enable_timeouts = false;
            break;

        case 'l':
            {
                printf("MaxScale assumed to be running locally; not started and logs not downloaded.");

                maxscale::start = false;
                maxscale::manual_debug = true;
                m_init_maxscale = false;
                m_no_maxscale_log_copy = true;
                m_shared.settings.local_maxscale = true;
            }
            break;

        case 'm':
            printf("Maxscale will be reinstalled");
            m_reinstall_maxscale = true;
            break;

        default:
            printf("UNKNOWN OPTION: %c\n", c);
            break;
        }
    }

    m_test_name = (optind < argc) ? argv[optind] : basename(argv[0]);
    return rval;
}

bool TestConnections::initialize_nodes()
{
    delete repl;
    repl = nullptr;
    std::future<bool> repl_future;
    bool use_repl = m_required_mdbci_labels.count(label_repl_be) > 0;
    if (use_repl)
    {
        repl = new mxt::ReplicationCluster(&m_shared);
        repl->setup(m_network_config);
        repl->set_use_ipv6(m_use_ipv6);
        repl->ssl = backend_ssl;
        repl_future = std::async(std::launch::async, &MariaDBCluster::check_nodes, repl);
    }

    delete galera;
    galera = nullptr;
    std::future<bool> galera_future;
    bool use_galera = m_required_mdbci_labels.count(label_galera_be) > 0;
    if (use_galera)
    {
        galera = new GaleraCluster(&m_shared);
        galera->setup(m_network_config);
        galera->set_use_ipv6(false);
        galera->ssl = backend_ssl;
        galera_future = std::async(std::launch::async, &GaleraCluster::check_nodes, galera);
    }

    delete xpand;
    xpand = nullptr;
    bool use_xpand = m_required_mdbci_labels.count(label_clx_be) > 0;
    if (use_xpand)
    {
        xpand = new XpandCluster(&m_shared);
        xpand->setup(m_network_config);
        xpand->set_use_ipv6(false);
        xpand->ssl = backend_ssl;
        xpand->fix_replication();
    }

    maxscales = new Maxscales(&m_shared);
    maxscales->setup(m_network_config);
    m_maxscale = std::make_unique<mxt::MaxScale>(maxscales, m_shared, 0);
    if (maxscales->N > 1)
    {
        m_maxscale2 = std::make_unique<mxt::MaxScale>(maxscales, m_shared, 1);
    }

    bool maxscale_ok = maxscales->check_nodes();
    bool repl_ok = !use_repl || repl_future.get();
    bool galera_ok = !use_galera || galera_future.get();
    return maxscale_ok && repl_ok && galera_ok;
}

bool TestConnections::check_backend_versions()
{
    auto tester = [this](MariaDBCluster* cluster, const string& required_vrs_str) {
            bool rval = true;
            if (cluster && !required_vrs_str.empty())
            {
                string found_vrs_str = cluster->get_lowest_version();
                int found_vrs = get_int_version(found_vrs_str);
                int required_vrs = get_int_version(required_vrs_str);

                if (found_vrs < required_vrs)
                {
                    tprintf("Test cluster '%s' version '%s' is too low, test '%s' requires at least '%s'.",
                            cluster->prefix().c_str(), found_vrs_str.c_str(),
                            m_test_name.c_str(), required_vrs_str.c_str());
                    rval = false;
                }
            }
            return rval;
        };

    auto repl_ok = tester(repl, maxscale::required_repl_version);
    auto galera_ok = tester(galera, maxscale::required_galera_version);
    return repl_ok && galera_ok;
}

bool TestConnections::required_machines_are_running()
{
    bool rval = false;
    StringSet missing_mdbci_labels;
    std::set_difference(m_required_mdbci_labels.begin(), m_required_mdbci_labels.end(),
                        m_configured_mdbci_labels.begin(), m_configured_mdbci_labels.end(),
                        std::inserter(missing_mdbci_labels, missing_mdbci_labels.begin()));

    if (missing_mdbci_labels.empty())
    {
        if (verbose())
        {
            tprintf("Machines with all required labels '%s' are running, MDBCI UP call is not needed",
                    m_required_mdbci_labels_str.c_str());
        }
        rval = true;
    }
    else
    {
        string missing_labels_str = flatten_stringset(missing_mdbci_labels);
        tprintf("Machines with labels '%s' are not running, MDBCI UP call is needed",
                missing_labels_str.c_str());
    }

    return rval;
}

void TestConnections::set_verbose(bool val)
{
    m_shared.verbose = val;
}

bool TestConnections::verbose() const
{
    return m_shared.verbose;
}

void TestConnections::write_node_env_vars()
{
    auto write_env_vars = [](Nodes* nodes) {
            if (nodes)
            {
                nodes->write_env_vars();
            }
        };

    write_env_vars(repl);
    write_env_vars(galera);
    write_env_vars(xpand);
    write_env_vars(maxscales);
}

int TestConnections::n_maxscales() const
{
    // A maximum of two MaxScales are supported so far. Defining only MaxScale2 is an error.
    int rval = 0;
    if (m_maxscale)
    {
        rval = (m_maxscale2) ? 2 : 1;
    }
    return rval;
}

int TestConnections::run_test(int argc, char* argv[], const std::function<int(TestConnections&)>& testfunc)
{
    int init_rc = prepare_for_test(argc, argv);
    int test_errors = 0;
    if (init_rc == 0)
    {
        test_errors = testfunc(*this);
    }
    int cleanup_rc = cleanup();

    // Return actual test error count only if init and cleanup succeed.
    int rval = 0;
    if (init_rc != 0)
    {
        rval = init_rc;
    }
    else if (cleanup_rc != 0)
    {
        rval = cleanup_rc;
    }
    else
    {
        rval = test_errors;
    }
    return rval;
}

void TestConnections::set_signal_handlers()
{
    signal_set(SIGSEGV, sigfatal_handler);
    signal_set(SIGABRT, sigfatal_handler);
    signal_set(SIGFPE, sigfatal_handler);
    signal_set(SIGILL, sigfatal_handler);
#ifdef SIGBUS
    signal_set(SIGBUS, sigfatal_handler);
#endif
}

bool TestConnections::check_create_vm_dir()
{
    bool rval = false;
    string mkdir_cmd = "mkdir -p " + m_mdbci_vm_path;
    if (run_shell_command(mkdir_cmd, "Failed to create MDBCI VMs directory."))
    {
        rval = true;
    }
    return rval;
}

bool TestConnections::run_shell_command(const string& cmd, const string& errmsg)
{
    bool rval = true;
    auto cmdc = cmd.c_str();

    int rc = system(cmdc);
    if (rc != 0)
    {
        rval = false;
        string msgp2 = mxb::string_printf("Shell command '%s' returned %i.", cmdc, rc);
        if (errmsg.empty())
        {
            add_failure("%s", msgp2.c_str());
        }
        else
        {
            add_failure("%s %s", errmsg.c_str(), msgp2.c_str());
        }
    }
    return rval;
}

std::string cutoff_string(const string& source, char cutoff)
{
    auto pos = source.find(cutoff);
    return (pos != string::npos) ? source.substr(0, pos) : source;
}

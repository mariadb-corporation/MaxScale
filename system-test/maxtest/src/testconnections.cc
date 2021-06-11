#include <getopt.h>
#include <libgen.h>
#include <stdarg.h>
#include <time.h>
#include <csignal>
#include <iostream>
#include <string>
#include <fstream>
#include <future>
#include <algorithm>

#include <maxbase/assert.h>
#include <maxbase/format.hh>
#include <maxbase/stacktrace.hh>
#include <maxbase/string.hh>
#include <maxbase/stopwatch.hh>

#include <maxtest/envv.hh>
#include <maxtest/galera_cluster.hh>
#include <maxtest/log.hh>
#include <maxtest/replication_cluster.hh>
#include <maxtest/mariadb_func.hh>
#include <maxtest/sql_t1.hh>
#include <maxtest/testconnections.hh>
#include <maxtest/test_info.hh>

using std::cout;
using std::endl;
using std::string;
using std::move;

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
static std::string required_repl_version;
static bool restart_galera = false;
static bool require_galera = false;
static bool require_columnstore = false;
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

static int call_system(const string& command)
{
    int rv = system(command.c_str());
    if (rv == -1)
    {
        printf("error: Could not execute '%s'.\n", command.c_str());
    }

    return rv;
}

void sigfatal_handler(int i)
{
    mxb::dump_stacktrace();
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
    int rc = prepare_for_test(argc, argv);
    if (rc != 0)
    {
        exit(rc);
    }
}

int TestConnections::prepare_for_test(int argc, char* argv[])
{
    if (m_init_done)
    {
        tprintf("ERROR: prepare_for_test called more than once.");
        return 1;
    }

    m_init_done = true;

    std::ios::sync_with_stdio(true);
    set_signal_handlers();

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

    int rc = setup_vms();
    if (rc != 0)
    {
        return rc;
    }

    // Stop MaxScale to prevent it from interfering with replication setup.
    if (!m_mxs_manual_debug)
    {
        stop_all_maxscales();
    }

    if (galera && maxscale::restart_galera)
    {
        galera->stop_nodes();
        galera->start_replication();
    }

    if (m_check_nodes)
    {
        // The replication cluster has extra backends if too many are configured due to a previously
        // ran big test. Hide the extra ones.
        if (repl)
        {
            repl->remove_extra_backends();
        }

        auto check_node = [&rc](MariaDBCluster* cluster) {
                if (cluster)
                {
                    if (!cluster->fix_replication() || !cluster->check_create_test_db())
                    {
                        rc = BROKEN_VM_FAIL;
                    }
                }
            };

        check_node(repl);
        check_node(galera);
        check_node(xpand);
    }

    if (rc == 0)
    {
        if (!check_backend_versions())
        {
            if (ok())
            {
                tprintf("Skipping test.");
                rc = TEST_SKIPPED;
            }
            else
            {
                rc = global_result;
            }
        }
    }

    if (rc == 0)
    {
        if (m_init_maxscale)
        {
            init_maxscales();
        }

        if (m_mdbci_called)
        {
            auto res = maxscale->ssh_output("maxscale --version-full", false);
            if (res.rc != 0)
            {
                tprintf("Error retrieving MaxScale version info");
            }
            else
            {
                tprintf("Maxscale_full_version_start:\n%s\nMaxscale_full_version_end\n", res.output.c_str());
            }
        }

        string create_logdir_cmd = "mkdir -p ";
        create_logdir_cmd += mxt::BUILD_DIR;
        create_logdir_cmd += "/LOGS/" + m_shared.test_name;
        if (run_shell_command(create_logdir_cmd, "Failed to create logs directory."))
        {
            if (m_enable_timeout)
            {
                m_timeout_thread = std::thread(&TestConnections::timeout_thread_func, this);
            }
            tprintf("Starting test");
            logger().reset_timer();
            rc = 0;
        }
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
    delete maxscale;
}

int TestConnections::cleanup()
{
    if (global_result > 0)
    {
        printf("\nTEST FAILURES:\n");
        printf("%s\n", logger().all_errors_to_string().c_str());
    }

    // Because cleanup is called even when system test init fails, we need to check fields exist before
    // access.
    if (!settings().local_maxscale)
    {
        // Stop all MaxScales to detect crashes on exit.
        bool sleep_more = false;
        for (int i = 0; i < n_maxscales(); i++)
        {
            auto mxs = my_maxscale(i);
            mxs->stop_and_check_stopped();
            if (mxs->use_valgrind())
            {
                sleep_more = true;
            }
        }

        if (sleep_more)
        {
            sleep(15);      // Sleep to allow more time for log writing. TODO: Really need 15s?
        }
    }

    if (m_fix_clusters_after)
    {
        mxt::BoolFuncArray funcs;
        if (repl)
        {
            funcs.push_back([this]() {
                                return repl->fix_replication();
                            });
        }
        if (galera)
        {
            funcs.push_back([this]() {
                                return galera->fix_replication();
                            });
        }
        if (xpand)
        {
            funcs.push_back([this]() {
                                return xpand->fix_replication();
                            });
        }
        m_shared.concurrent_run(funcs);
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
    m_cleaned_up = true;
    return 0;
}

int TestConnections::setup_vms()
{
    auto call_mdbci_and_check = [this](const char* mdbci_options = "") {
            bool vms_found = false;
            if (call_mdbci(mdbci_options))
            {
                m_mdbci_called = true;
                // Network config should exist now.
                if (read_network_config())
                {
                    if (required_machines_are_running())
                    {
                        vms_found = true;
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
            else
            {
                add_failure("MDBCI failed.");
            }
            return vms_found;
        };

    bool maxscale_installed = false;

    bool vms_found = false;
    if (read_network_config() && required_machines_are_running())
    {
        vms_found = true;
    }
    else
    {
        // Not all VMs were found. Call MDBCI first time.
        if (call_mdbci_and_check())
        {
            vms_found = true;
            maxscale_installed = true;
        }
    }

    bool nodes_ok = false;
    if (vms_found)
    {
        if (initialize_nodes())
        {
            nodes_ok = true;
        }
        else
        {
            tprintf("Recreating VMs because node check failed.");
        }

        if (!nodes_ok)
        {
            // Node init failed, call mdbci again. Is this worth even trying?
            if (call_mdbci_and_check("--recreate"))
            {
                maxscale_installed = true;
                if (initialize_nodes())
                {
                    nodes_ok = true;
                }
            }

            if (!nodes_ok)
            {
                add_failure("Could not initialize nodes even after 'mdbci --recreate'. Exiting.");
            }
        }
    }

    int rval = MDBCI_FAIL;
    if (nodes_ok)
    {
        rval = 0;
        if (m_reinstall_maxscale)
        {
            if (reinstall_maxscales())
            {
                maxscale_installed = true;
            }
            else
            {
                add_failure("Failed to install Maxscale: target is %s", m_target.c_str());
                rval = MDBCI_FAIL;
            }
        }

        if (rval == 0 && maxscale_installed)
        {
            string src = string(mxt::SOURCE_DIR) + "/mdbci/add_core_cnf.sh";
            maxscale->copy_to_node(src.c_str(), maxscale->access_homedir());
            maxscale->ssh_node_f(true, "%s/add_core_cnf.sh %s", maxscale->access_homedir(),
                                 verbose() ? "verbose" : "");
        }
    }

    return rval;
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
 * if the VM setup has not yet been initialized (first test of the test run).
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
    maxscale_ssl = readenv_bool("ssl", false);
    m_use_ipv6 = readenv_bool("use_ipv6", false);
    backend_ssl = readenv_bool("backend_ssl", false);
    smoke = readenv_bool("smoke", true);
    m_threads = readenv_int("threads", 4);
    m_maxscale_log_copy = !readenv_bool("no_maxscale_log_copy", false);

    if (readenv_bool("no_nodes_check", false))
    {
        m_check_nodes = false;
    }

    if (readenv_bool("no_maxscale_start", false))
    {
        maxscale::start = false;
    }

    // The following settings are final, and not modified by either command line parameters or mdbci.
    m_backend_log_copy = !readenv_bool("no_backend_log_copy", false);
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
        if (test->name == m_shared.test_name)
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
                m_shared.test_name.c_str(), m_cnf_template_path.c_str(), found->labels,
                m_required_mdbci_labels_str.c_str());

        if (test_labels.count("BACKEND_SSL") > 0)
        {
            backend_ssl = true;
        }

        if (test_labels.count("LISTENER_SSL") > 0)
        {
            maxscale_ssl = true;
        }
    }
    else
    {
        add_failure("Could not find '%s' in the CMake-generated test definitions array.",
                    m_shared.test_name.c_str());
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
bool
TestConnections::process_template(MaxScale& mxs, const string& config_file_path, const char* dest)
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
            auto& nw_conf_prefix = cluster->nwconf_prefix();

            for (int i = 0; i < cluster->N; i++)
            {
                // These placeholders in the config template use the network config node name prefix,
                // not the MaxScale config server name prefix.
                string ip_ph = mxb::string_printf("###%s_server_IP_%0d###", nw_conf_prefix.c_str(), i + 1);
                string ip_str = using_ip6 ? cluster->ip6(i) : cluster->ip_private(i);
                replace_text(ip_ph, ip_str);

                string port_ph = mxb::string_printf("###%s_server_port_%0d###",
                                                    nw_conf_prefix.c_str(), i + 1);
                string port_str = std::to_string(cluster->port[i]);
                replace_text(port_ph, port_str);
            }

            // The following generates basic server definitions for all servers. These, confusingly,
            // use the MaxScale config file name prefix in the placeholder.
            string all_servers_ph = mxb::string_printf("###%s###", cluster->cnf_server_prefix().c_str());
            string all_servers_str = cluster->cnf_servers();
            replace_text(all_servers_ph, all_servers_str);

            // The following generates one line with server names. Used with monitors and services.
            string all_servers_line_ph = mxb::string_printf("###%s_line###",
                                                            cluster->cnf_server_prefix().c_str());
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
                                   "\"s|type *= *server|type=server\\nssl=required\\nssl_cert=/###access_homedir###/"
                                   "certs/client-cert.pem\\nssl_key=/###access_homedir###/certs/client-key.pem"
                                   "\\nssl_ca_cert=/###access_homedir###/certs/ca.pem\\nssl_cert_verify_depth=9"
                                   "\\nssl_version=MAX|g\" maxscale.cnf";
            run_shell_command(sed_cmd, edit_failed);
        }

        string sed_cmd = mxb::string_printf("sed -i \"s/###access_user###/%s/g\" %s",
                                            mxs.access_user(), target_file.c_str());
        run_shell_command(sed_cmd, edit_failed);

        sed_cmd = mxb::string_printf("sed -i \"s|###access_homedir###|%s|g\" %s",
                                     mxs.access_homedir(), target_file.c_str());
        run_shell_command(sed_cmd, edit_failed);

        mxs.vm_node().copy_to_node("maxscale.cnf", dest);
        rval = true;
    }
    else
    {
        int eno = errno;
        add_failure("Could not write to '%s'. Error %i, %s", target_file.c_str(), eno, mxb_strerror(eno));
    }
    return rval;
}

/**
 * Copy maxscale.cnf and start MaxScale on all Maxscale VMs.
 */
void TestConnections::init_maxscales()
{
    // Always initialize the first MaxScale
    init_maxscale(0);

    if (m_required_mdbci_labels.count(label_2nd_mxs))
    {
        init_maxscale(1);
    }
    else if (n_maxscales() > 1)
    {
        // Second MaxScale exists but is not required by test.
        my_maxscale(1)->stop();
    }
}

void TestConnections::init_maxscale(int m)
{
    auto mxs = my_maxscale(m);
    auto homedir = mxs->access_homedir();
    // The config file path can be multivalued when running a test with multiple MaxScales.
    // Select the correct file.
    auto filepaths = mxb::strtok(m_cnf_template_path, ";");
    int n_files = filepaths.size();
    if (m < n_files)
    {
        // Have a separate config file for this MaxScale.
        process_template(*mxs, filepaths[m], homedir);
    }
    else if (n_files >= 1)
    {
        // Not enough config files given for all MaxScales. Use the config of first MaxScale. This can
        // happen with the "check_backends"-test.
        tprintf("MaxScale %i does not have a designated config file, only found %i files in test definition. "
                "Using main MaxScale config file instead.", m, n_files);
        process_template(*mxs, filepaths[0], homedir);
    }
    else
    {
        tprintf("No MaxScale config files defined. MaxScale may not start.");
    }

    if (mxs->ssh_node_f(true, "test -d %s/certs", homedir))
    {
        tprintf("SSL certificates not found, copying to maxscale");
        mxs->ssh_node_f(true, "rm -rf %s/certs;mkdir -m a+wrx %s/certs;", homedir, homedir);

        char str[4096];
        char dtr[4096];
        sprintf(str, "%s/ssl-cert/*", mxt::SOURCE_DIR);
        sprintf(dtr, "%s/certs/", homedir);
        mxs->copy_to_node(str, dtr);
        sprintf(str, "cp %s/ssl-cert/* .", mxt::SOURCE_DIR);
        call_system(str);
        mxs->ssh_node_f(true, "chmod -R a+rx %s;", homedir);
    }

    mxs->ssh_node_f(true,
                    "cp maxscale.cnf %s;"
                    "iptables -F INPUT;"
                    "rm -rf %s/*.log /tmp/core* /dev/shm/* /var/lib/maxscale/* /var/lib/maxscale/.secrets;"
                    "find /var/*/maxscale -name 'maxscale.lock' -delete;",
                    mxs->maxscale_cnf.c_str(),
                    mxs->maxscale_log_dir.c_str());
    if (maxscale::start)
    {
        mxs->restart_maxscale();
        mxs->ssh_node_f(true, "maxctrl api get maxscale/debug/monitor_wait");
    }
    else
    {
        mxs->stop_maxscale();
    }
}

/**
 * Copies all MaxScale logs and (if happens) core to current workspace
 */
void TestConnections::copy_all_logs()
{
    string str = mxb::string_printf("mkdir -p %s/LOGS/%s", mxt::BUILD_DIR, m_shared.test_name.c_str());
    call_system(str);

    if (m_backend_log_copy)
    {
        if (repl)
        {
            repl->copy_logs("node");
        }
        if (galera)
        {
            galera->copy_logs("galera");
        }
    }

    if (m_maxscale_log_copy)
    {
        copy_maxscale_logs(0);
    }
}

/**
 * Copies logs from all MaxScales.
 *
 * @param timestamp The timestamp to add to log file directory. 0 means no timestamp.
 */
void TestConnections::copy_maxscale_logs(int timestamp)
{
    for (int i = 0; i < n_maxscales(); i++)
    {
        auto mxs = my_maxscale(i);
        mxs->copy_log(i, timestamp, m_shared.test_name);
    }
}

/**
 * Copies all MaxScale logs and (if happens) core to current workspace and
 * sends time stamp to log copying script
 */
void TestConnections::copy_all_logs_periodic()
{
    copy_maxscale_logs(logger().time_elapsed_s());
}

void TestConnections::revert_replicate_from_master()
{
    repl->connect();
    execute_query(repl->nodes[0], "RESET MASTER");

    for (int i = 1; i < repl->N; i++)
    {
        repl->set_slave(repl->nodes[i], repl->ip_private(0), repl->port[0]);
        execute_query(repl->nodes[i], "start slave");
    }
}

int TestConnections::start_mm()
{
    tprintf("Stopping maxscale\n");
    int rval = maxscale->stop_maxscale();

    tprintf("Stopping all backend nodes\n");
    rval += repl->stop_nodes() ? 0 : 1;

    for (int i = 0; i < 2; i++)
    {
        tprintf("Starting back node %d\n", i);
        rval += repl->start_node(i, (char*) "");
    }

    repl->connect();
    for (int i = 0; i < 2; i++)
    {
        execute_query(repl->nodes[i], "stop slave");
        execute_query(repl->nodes[i], "reset master");
    }

    execute_query(repl->nodes[0], "SET GLOBAL READ_ONLY=ON");

    repl->set_slave(repl->nodes[0], repl->ip_private(1), repl->port[1]);
    repl->set_slave(repl->nodes[1], repl->ip_private(0), repl->port[0]);

    repl->close_connections();

    tprintf("Starting back Maxscale\n");
    rval += maxscale->start_maxscale();

    return rval;
}

bool TestConnections::log_matches(const char* pattern)
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

    return maxscale->ssh_node_f(true, "grep '%s' /var/log/maxscale/maxscale*.log", p.c_str()) == 0;
}

void TestConnections::log_includes(const char* pattern)
{
    add_result(!log_matches(pattern), "Log does not match pattern '%s'", pattern);
}

void TestConnections::log_excludes(const char* pattern)
{
    add_result(log_matches(pattern), "Log matches pattern '%s'", pattern);
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

int TestConnections::find_connected_slave1()
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    repl->connect();
    for (int i = 0; i < repl->N; i++)
    {
        conn_num = get_conn_num(repl->nodes[i], maxscale->ip(), maxscale->hostname(), (char*) "test");
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

bool TestConnections::stop_all_maxscales()
{
    bool rval = true;
    for (int i = 0; i < n_maxscales(); i++)
    {
        if (my_maxscale(i)->stop_maxscale() != 0)
        {
            rval = false;
        }
    }
    return rval;
}

int TestConnections::check_maxscale_alive()
{
    int gr = global_result;
    tprintf("Connecting to Maxscale\n");
    add_result(maxscale->connect_maxscale(), "Can not connect to Maxscale\n");
    tprintf("Trying simple query against all sevices\n");
    tprintf("RWSplit \n");
    try_query(maxscale->conn_rwsplit[0], "show databases;");
    tprintf("ReadConn Master \n");
    try_query(maxscale->conn_master, "show databases;");
    tprintf("ReadConn Slave \n");
    try_query(maxscale->conn_slave, "show databases;");
    maxscale->close_maxscale_connections();
    add_result(global_result - gr, "Maxscale is not alive\n");
    my_maxscale(0)->expect_running_status(true);

    return global_result - gr;
}

int TestConnections::test_maxscale_connections(bool rw_split, bool rc_master, bool rc_slave)
{
    int rval = 0;
    int rc;

    tprintf("Testing RWSplit, expecting %s\n", (rw_split ? "success" : "failure"));
    rc = execute_query(maxscale->conn_rwsplit[0], "select 1");
    if ((rc == 0) != rw_split)
    {
        tprintf("Error: Query %s\n", (rw_split ? "failed" : "succeeded"));
        rval++;
    }

    tprintf("Testing ReadConnRoute Master, expecting %s\n", (rc_master ? "success" : "failure"));
    rc = execute_query(maxscale->conn_master, "select 1");
    if ((rc == 0) != rc_master)
    {
        tprintf("Error: Query %s", (rc_master ? "failed" : "succeeded"));
        rval++;
    }

    tprintf("Testing ReadConnRoute Slave, expecting %s\n", (rc_slave ? "success" : "failure"));
    rc = execute_query(maxscale->conn_slave, "select 1");
    if ((rc == 0) != rc_slave)
    {
        tprintf("Error: Query %s", (rc_slave ? "failed" : "succeeded"));
        rval++;
    }
    return rval;
}


int TestConnections::create_connections(int conn_N, bool rwsplit_flag, bool master_flag, bool slave_flag,
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

            rwsplit_conn[i] = maxscale->open_rwsplit_connection();
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

            master_conn[i] = maxscale->open_readconn_master_connection();
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

            slave_conn[i] = maxscale->open_readconn_slave_connection();
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
                open_conn(4016, maxscale->ip4(), maxscale->user_name, maxscale->password, maxscale_ssl);
            if (mysql_errno(galera_conn[i]) != 0)
            {
                local_result++;
                tprintf("Galera connection failed, error: %s\n", mysql_error(galera_conn[i]));
            }
        }
    }
    for (i = 0; i < conn_N; i++)
    {
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

    return local_result;
}

void TestConnections::reset_timeout()
{
    m_reset_timeout = true;
}

void TestConnections::set_log_copy_interval(uint32_t interval_seconds)
{
    // Add disabling if required. Currently periodic log copying is only used by a few long tests.
    mxb_assert(interval_seconds > 0);
    m_log_copy_interval = interval_seconds;

    // Assume that log copy thread not yet created. Start it. Calling this function twice in a test
    // will crash.
    m_log_copy_thread = std::thread(&TestConnections::log_copy_thread_func, this);
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

    maxscale->ssh_node_f(true, "echo '--- %s ---' >> /var/log/maxscale/maxscale.log", buf);
}

int TestConnections::get_master_server_id()
{
    int master_id = -1;
    MYSQL* conn = maxscale->open_rwsplit_connection();
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

/**
 * Thread which terminates test application if it seems stuck.
 */
void TestConnections::timeout_thread_func()
{
    auto timeout_start = mxb::Clock::now();
    auto timeout_limit = mxb::from_secs(300);
    auto relax = std::memory_order_relaxed;

    while (!m_stop_threads.load(relax))
    {
        auto now = mxb::Clock::now();
        if (m_reset_timeout.load(relax))
        {
            timeout_start = now;
            m_reset_timeout = false;
        }

        if (now - timeout_start > timeout_limit)
        {
            logger().add_failure("**** Timeout reached! Copying logs and exiting. ****");
            copy_all_logs();
            exit(250);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

/**
 * Function which periodically copies logs from Maxscale machine.
 */
void TestConnections::log_copy_thread_func()
{
    logger().log_msg("**** Periodic log copy thread started ****");
    auto last_log_copy = mxb::Clock::now();
    auto interval = mxb::from_secs(m_log_copy_interval);

    while (!m_stop_threads.load(std::memory_order_relaxed))
    {
        auto now = mxb::Clock::now();
        if (now - last_log_copy > interval)
        {
            logger().log_msg("**** Copying all logs ****");
            copy_all_logs_periodic();
            last_log_copy = now;
        }
        sleep(1);
    }

    logger().log_msg("**** Periodic log copy thread exiting ****");
}

int TestConnections::insert_select(int N)
{
    int result = 0;

    tprintf("Create t1\n");
    create_t1(maxscale->conn_rwsplit[0]);

    tprintf("Insert data into t1\n");
    insert_into_t1(maxscale->conn_rwsplit[0], N);
    repl->sync_slaves();

    tprintf("SELECT: rwsplitter\n");
    result += select_from_t1(maxscale->conn_rwsplit[0], N);

    tprintf("SELECT: master\n");
    result += select_from_t1(maxscale->conn_master, N);

    tprintf("SELECT: slave\n");
    result += select_from_t1(maxscale->conn_slave, N);

    return result;
}

int TestConnections::use_db(char* db)
{
    int local_result = 0;
    char sql[100];

    sprintf(sql, "USE %s;", db);
    tprintf("selecting DB '%s' for rwsplit\n", db);
    local_result += execute_query(maxscale->conn_rwsplit[0], "%s", sql);
    tprintf("selecting DB '%s' for readconn master\n", db);
    local_result += execute_query(maxscale->conn_master, "%s", sql);
    tprintf("selecting DB '%s' for readconn slave\n", db);
    local_result += execute_query(maxscale->conn_slave, "%s", sql);
    for (int i = 0; i < repl->N; i++)
    {
        tprintf("selecting DB '%s' for direct connection to node %d\n", db, i);
        local_result += execute_query(repl->nodes[i], "%s", sql);
    }
    return local_result;
}

int TestConnections::check_t1_table(bool presence, char* db)
{
    const char* expected = presence ? "" : "NOT";
    const char* actual = presence ? "NOT" : "";
    int start_result = global_result;

    add_result(use_db(db), "use db failed\n");
    repl->sync_slaves();

    tprintf("Checking: table 't1' should %s be found in '%s' database\n", expected, db);
    int exists = check_if_t1_exists(maxscale->conn_rwsplit[0]);

    if (exists == presence)
    {
        tprintf("RWSplit: ok\n");
    }
    else
    {
        add_result(1, "Table t1 is %s found in '%s' database using RWSplit\n", actual, db);
    }

    exists = check_if_t1_exists(maxscale->conn_master);

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

    exists = check_if_t1_exists(maxscale->conn_slave);

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

StringSet TestConnections::get_server_status(const std::string& name)
{
    return maxscale->get_server_status(name);
}

void TestConnections::check_current_operations(int value)
{
    for (int i = 0; i < repl->N; i++)
    {
        auto res = maxctrl("api get servers/server"
                           + std::to_string(i + 1)
                           + " data.attributes.statistics.active_operations");

        expect(std::stoi(res.output) == value,
               "Current no. of operations is not %d for server%d", value, i + 1);
    }
}

void TestConnections::check_current_connections(int value)
{
    for (int i = 0; i < repl->N; i++)
    {
        auto res = maxctrl("api get servers/server"
                           + std::to_string(i + 1)
                           + " data.attributes.statistics.connections");

        expect(std::stoi(res.output) == value,
               "Current no. of conns is not %d for server%d", value, i + 1);
    }
}

bool TestConnections::test_bad_config(const string& config)
{
    process_template(*maxscale, config, "/tmp/");

    int ssh_rc = maxscale->ssh_node_f(true,
                                      "cp /tmp/maxscale.cnf /etc/maxscale.cnf; pkill -9 maxscale; "
                                      "maxscale -U maxscale -lstdout &> /dev/null && sleep 1 && pkill -9 maxscale");
    return (ssh_rc == 0) || (ssh_rc == 256);
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
                string copy_cmd = mxb::string_printf("cp -r %s/mdbci/cnf %s/",
                                                     mxt::SOURCE_DIR, m_vm_path.c_str());
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
                                              mxt::SOURCE_DIR, m_mdbci_template.c_str());
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

bool TestConnections::reinstall_maxscales()
{
    bool rval = true;
    for (int i = 0; i < n_maxscales(); i++)
    {
        if (!my_maxscale(i)->reinstall(m_target, m_mdbci_config_name))
        {
            rval = false;
        }
    }
    return rval;
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
        {"serial-run",         no_argument,       0, 'e'},
        {"fix-clusters",       no_argument,       0, 'f'},
        {0,                    0,                 0, 0  }
    };

    bool rval = true;
    int c;
    int option_index = 0;

    while ((c = getopt_long(argc, argv, "hvnqsirgzlmef::", long_options, &option_index)) != -1)
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
            m_mxs_manual_debug = true;
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
            m_enable_timeout = false;
            break;

        case 'l':
            {
                printf("MaxScale assumed to be running locally; not started and logs not downloaded.\n");
                maxscale::start = false;
                m_mxs_manual_debug = true;
                m_init_maxscale = false;
                m_maxscale_log_copy = false;
                m_shared.settings.local_maxscale = true;
            }
            break;

        case 'm':
            printf("Maxscale will be reinstalled.\n");
            m_reinstall_maxscale = true;
            break;

        case 'e':
            printf("Preferring serial execution.\n");
            m_shared.settings.allow_concurrent_run = false;
            break;

        case 'f':
            printf("Fixing clusters after test.\n");
            m_fix_clusters_after = true;
            break;

        default:
            printf("UNKNOWN OPTION: %c\n", c);
            break;
        }
    }

    m_shared.test_name = (optind < argc) ? argv[optind] : basename(argv[0]);
    return rval;
}

bool TestConnections::initialize_nodes()
{
    const char errmsg[] = "Failed to setup node group %s.";
    bool error = false;
    mxt::BoolFuncArray funcs;

    auto initialize_cluster = [&](MariaDBCluster* new_cluster, int n_min_expected, bool ipv6, bool be_ssl) {
            if (new_cluster->setup(m_network_config, n_min_expected))
            {
                new_cluster->set_use_ipv6(ipv6);
                new_cluster->set_use_ssl(be_ssl);
                auto prepare_cluster = [new_cluster]() {
                        return new_cluster->basic_test_prepare();
                    };
                funcs.push_back(move(prepare_cluster));
            }
            else
            {
                error = true;
                add_failure(errmsg, new_cluster->name().c_str());
            }
        };

    delete repl;
    repl = nullptr;
    bool use_repl = m_required_mdbci_labels.count(label_repl_be) > 0;
    if (use_repl)
    {
        bool big_repl = m_required_mdbci_labels.count(label_big_be) > 0;
        int n_min_expected = big_repl ? 15 : 4;
        repl = new mxt::ReplicationCluster(&m_shared);
        initialize_cluster(repl, n_min_expected, m_use_ipv6, backend_ssl);
    }

    delete galera;
    galera = nullptr;
    bool use_galera = m_required_mdbci_labels.count(label_galera_be) > 0;
    if (use_galera)
    {
        galera = new GaleraCluster(&m_shared);
        initialize_cluster(galera, 4, false, backend_ssl);
    }

    delete xpand;
    xpand = nullptr;
    bool use_xpand = m_required_mdbci_labels.count(label_clx_be) > 0;
    if (use_xpand)
    {
        xpand = new XpandCluster(&m_shared);
        initialize_cluster(xpand, 4, false, backend_ssl);
    }

    auto initialize_maxscale = [this, &funcs](MaxScale*& mxs_storage, int vm_ind) {
            delete mxs_storage;
            mxs_storage = nullptr;
            string vm_name = mxb::string_printf("%s_%03d", MaxScale::prefix().c_str(), vm_ind);

            auto new_maxscale = std::make_unique<MaxScale>(&m_shared);
            if (new_maxscale->setup(m_network_config, vm_name))
            {
                new_maxscale->set_use_ipv6(m_use_ipv6);
                new_maxscale->set_ssl(maxscale_ssl);

                mxs_storage = new_maxscale.release();

                auto prepare_maxscales = [mxs_storage]() {
                        return mxs_storage->prepare_for_test();
                    };
                funcs.push_back(move(prepare_maxscales));
            }
        };

    initialize_maxscale(maxscale, 0);
    // Try to setup MaxScale2 even if test does not need it. It could be running and should be
    // shut down when not used.
    initialize_maxscale(maxscale2, 1);

    int n_mxs_inited = n_maxscales();
    int n_mxs_expected = (m_required_mdbci_labels.count(label_2nd_mxs) > 0) ? 2 : 1;
    if (n_mxs_inited < n_mxs_expected)
    {
        error = true;
        add_failure("Not enough MaxScales. Test requires %i, found %i.",
                    n_mxs_expected, n_mxs_inited);
    }
    else if (n_mxs_inited > 1 && settings().local_maxscale)
    {
        error = true;
        add_failure("Multiple MaxScales are defined while using a local MaxScale. Not supported.");
    }

    return error ? false : m_shared.concurrent_run(funcs);
}

bool TestConnections::check_backend_versions()
{
    auto tester = [](MariaDBCluster* cluster, const string& required_vrs_str) {
            bool rval = true;
            if (cluster && !required_vrs_str.empty())
            {
                int required_vrs = get_int_version(required_vrs_str);
                rval = cluster->check_backend_versions(required_vrs);
            }
            return rval;
        };

    auto repl_ok = tester(repl, maxscale::required_repl_version);
    return repl_ok;
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
    m_shared.settings.verbose = val;
}

bool TestConnections::verbose() const
{
    return m_shared.settings.verbose;
}

void TestConnections::write_node_env_vars()
{
    auto write_env_vars = [](MariaDBCluster* cluster) {
            if (cluster)
            {
                cluster->write_env_vars();
            }
        };

    write_env_vars(repl);
    write_env_vars(galera);
    write_env_vars(xpand);
    if (maxscale)
    {
        maxscale->write_env_vars();
    }
}

int TestConnections::n_maxscales() const
{
    // A maximum of two MaxScales are supported so far. Defining only the second MaxScale is an error.
    int rval = 0;
    if (maxscale)
    {
        rval = maxscale2 ? 2 : 1;
    }
    return rval;
}

int TestConnections::run_test(int argc, char* argv[], const std::function<void(TestConnections&)>& testfunc)
{
    int init_rc = prepare_for_test(argc, argv);
    int test_errors = 0;
    if (init_rc == 0)
    {
        testfunc(*this);
        test_errors = global_result;
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
    return m_shared.run_shell_command(cmd, errmsg);
}

mxt::MariaDBServer* TestConnections::get_repl_master()
{
    mxt::MariaDBServer* rval = nullptr;
    if (repl)
    {
        auto server_info = maxscale->get_servers();
        for (size_t i = 0; i < server_info.size() && !rval; i++)
        {
            auto& info = server_info.get(i);
            if (info.status & mxt::ServerInfo::MASTER)
            {
                for (int j = 0; j < repl->N; j++)
                {
                    auto* be = repl->backend(j);
                    if (be->status().server_id == info.server_id)
                    {
                        rval = be;
                        break;
                    }
                }
            }
        }
    }
    return rval;
}

/**
 * Helper function for selecting correct MaxScale.
 *
 * @param m Index, 0 or 1.
 * @return MaxScale object
 */
MaxScale* TestConnections::my_maxscale(int m) const
{
    MaxScale* rval = nullptr;
    if (m == 0)
    {
        rval = maxscale;
    }
    else if (m == 1)
    {
        rval = maxscale2;
    }
    return rval;
}

/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/maxscales.hh>
#include <string>
#include <maxbase/format.hh>
#include <maxbase/jansson.hh>
#include <maxbase/json.hh>
#include <maxbase/string.hh>
#include <maxtest/log.hh>
#include <maxtest/mariadb_connector.hh>
#include <maxtest/testconnections.hh>
#include "envv.hh"

using std::string;

namespace
{
const string my_prefix = "maxscale";
enum class StatusType {STATUS, DETAIL};

struct ServerStatusDesc
{
    mxt::ServerInfo::bitfield bit {0};
    StatusType                type {StatusType::STATUS};
    string                    desc;
};

const ServerStatusDesc status_flag_to_str[] = {
    {mxt::ServerInfo::MASTER,     StatusType::STATUS, "Master"                  },
    {mxt::ServerInfo::SLAVE,      StatusType::STATUS, "Slave"                   },
    {mxt::ServerInfo::RUNNING,    StatusType::STATUS, "Running"                 },
    {mxt::ServerInfo::DOWN,       StatusType::STATUS, "Down"                    },
    {mxt::ServerInfo::MAINT,      StatusType::STATUS, "Maintenance"             },
    {mxt::ServerInfo::DRAINING,   StatusType::STATUS, "Draining"                },
    {mxt::ServerInfo::DRAINED,    StatusType::STATUS, "Drained"                 },
    {mxt::ServerInfo::RELAY,      StatusType::STATUS, "Relay Master"            },
    {mxt::ServerInfo::BLR,        StatusType::STATUS, "Binlog Relay"            },
    {mxt::ServerInfo::SYNCED,     StatusType::STATUS, "Synced"                  },
    {mxt::ServerInfo::EXT_MASTER, StatusType::DETAIL, "Slave of External Server"},
    {mxt::ServerInfo::DISK_LOW,   StatusType::DETAIL, "Low disk space"          }};
}

namespace maxtest
{
MaxScale::MaxScale(mxt::SharedData* shared)
    : m_shared(*shared)
{
}

MaxScale::~MaxScale()
{
    close_maxscale_connections();
}

bool MaxScale::setup(const mxt::NetworkConfig& nwconfig, const std::string& vm_name)
{
    auto prefixc = my_prefix.c_str();
    string key_user = mxb::string_printf("%s_user", prefixc);
    m_user_name = envvar_get_set(key_user.c_str(), "skysql");

    string key_pw = mxb::string_printf("%s_password", prefixc);
    m_password = envvar_get_set(key_pw.c_str(), "skysql");

    m_use_valgrind = readenv_bool("use_valgrind", false);
    m_use_callgrind = readenv_bool("use_callgrind", false);
    if (m_use_callgrind)
    {
        m_use_valgrind = true;
    }

    m_vmnode = nullptr;
    bool rval = false;

    auto new_node = std::make_unique<mxt::VMNode>(m_shared, vm_name, "mariadb");
    if (new_node->configure(nwconfig))
    {
        m_vmnode = move(new_node);

        string key_cnf = vm_name + "_cnf";
        m_cnf_path = envvar_get_set(key_cnf.c_str(), "/etc/maxscale.cnf");

        string key_log_dir = vm_name + "_log_dir";
        string log_dir = envvar_get_set(key_log_dir.c_str(), "/var/log/maxscale");
        set_log_dir(std::move(log_dir));

        rwsplit_port = 4006;
        readconn_master_port = 4008;
        readconn_slave_port = 4009;

        ports[0] = rwsplit_port;
        ports[1] = readconn_master_port;
        ports[2] = readconn_slave_port;

        // TODO: think of a proper reset command if ever needed.
        m_vmnode->set_start_stop_reset_cmds("systemctl restart maxscale",
                                            "systemctl stop maxscale",
                                            "");
        rval = true;
    }
    return rval;
}

bool MaxScale::setup(const mxb::ini::map_result::Configuration::value_type& config)
{
    bool rval = false;
    auto new_node = mxt::create_node(config, m_shared);
    if (new_node)
    {
        auto& cnf = config.second;
        auto& s = m_shared;
        string log_dir;
        if (s.read_str(cnf, "cnf_path", m_cnf_path)
            && s.read_str(cnf, "mxs_logdir", log_dir)
            && s.read_str(cnf, "log_storage_dir", m_log_storage_dir)
            && s.read_str(cnf, "mariadb_username", m_user_name)
            && s.read_str(cnf, "mariadb_password", m_password)
            && s.read_str(cnf, "maxctrl_cmd", m_local_maxctrl)
            && s.read_int(cnf, "rwsplit_port", rwsplit_port)
            && s.read_int(cnf, "rcrmaster_port", readconn_master_port)
            && s.read_int(cnf, "rcrslave_port", readconn_slave_port))
        {
            ports[0] = rwsplit_port;
            ports[1] = readconn_master_port;
            ports[2] = readconn_slave_port;
            set_log_dir(std::move(log_dir));
            m_vmnode = std::move(new_node);
            rval = true;
        }
        else
        {
            log().add_failure("Could not configure MaxScale node '%s'.", config.first.c_str());
        }
    }
    return rval;
}

int MaxScale::connect_rwsplit(const std::string& db)
{
    mysql_close(conn_rwsplit);

    conn_rwsplit = open_conn_db(rwsplit_port, ip(), db, m_user_name, m_password, m_ssl);
    routers[0] = conn_rwsplit;

    int rc = 0;
    int my_errno = mysql_errno(conn_rwsplit);

    if (my_errno)
    {
        if (verbose())
        {
            printf("Failed to connect to readwritesplit: %d, %s\n", my_errno, mysql_error(conn_rwsplit));
        }
        rc = my_errno;
    }

    return rc;
}

int MaxScale::connect_readconn_master(const std::string& db)
{
    MYSQL*& conn_rc_master = conn_master;
    mysql_close(conn_rc_master);

    conn_rc_master = open_conn_db(readconn_master_port, ip(), db, m_user_name, m_password, m_ssl);
    routers[1] = conn_rc_master;

    int rc = 0;
    int my_errno = mysql_errno(conn_rc_master);

    if (my_errno)
    {
        if (verbose())
        {
            printf("Failed to connect to readwritesplit: %d, %s\n", my_errno, mysql_error(conn_rc_master));
        }
        rc = my_errno;
    }

    return rc;
}

int MaxScale::connect_readconn_slave(const std::string& db)
{
    MYSQL*& conn_rc_slave = conn_slave;
    mysql_close(conn_rc_slave);

    conn_rc_slave = open_conn_db(readconn_slave_port, ip(), db, m_user_name, m_password, m_ssl);
    routers[2] = conn_rc_slave;

    int rc = 0;
    int my_errno = mysql_errno(conn_rc_slave);

    if (my_errno)
    {
        if (verbose())
        {
            printf("Failed to connect to readwritesplit: %d, %s\n", my_errno, mysql_error(conn_rc_slave));
        }
        rc = my_errno;
    }

    return rc;
}

int MaxScale::connect_maxscale(const std::string& db)
{
    return connect_rwsplit(db) + connect_readconn_master(db) + connect_readconn_slave(db);
}

int MaxScale::connect(const std::string& db)
{
    return connect_maxscale(db);
}

int MaxScale::close_maxscale_connections()
{
    close_readconn_master();

    mysql_close(conn_slave);
    conn_slave = nullptr;

    close_rwsplit();
    return 0;
}

int MaxScale::disconnect()
{
    return close_maxscale_connections();
}

int MaxScale::restart_maxscale()
{
    int res;
    if (m_use_valgrind)
    {
        res = stop_maxscale();
        res += start_maxscale();
    }
    else
    {
        if (m_vmnode->is_remote())
        {
            res = m_vmnode->start_process("") ? 0 : 1;
        }
        else
        {
            res = start_local_maxscale();
        }
    }
    return res;
}

int MaxScale::start_maxscale()
{
    int res;
    if (m_use_valgrind)
    {
        auto log_dir = m_log_dir.c_str();
        if (m_use_callgrind)
        {
            res = ssh_node_f(false,
                             "sudo --user=maxscale valgrind -d "
                             "--log-file=/%s/valgrind%02d.log --trace-children=yes "
                             " --tool=callgrind --callgrind-out-file=/%s/callgrind%02d.log "
                             " /usr/bin/maxscale",
                             log_dir, m_valgrind_log_num,
                             log_dir, m_valgrind_log_num);
        }
        else
        {
            res = ssh_node_f(false,
                             "sudo --user=maxscale valgrind --leak-check=full --show-leak-kinds=all "
                             "--log-file=/%s/valgrind%02d.log --trace-children=yes "
                             "--track-origins=yes /usr/bin/maxscale",
                             log_dir, m_valgrind_log_num);
        }
        m_valgrind_log_num++;
    }
    else
    {
        if (m_vmnode->is_remote())
        {
            res = m_vmnode->start_process("") ? 0 : 1;
        }
        else
        {
            res = start_local_maxscale();
        }
    }
    return res;
}

int MaxScale::start_local_maxscale()
{
    // MaxScale running locally, first stop it. In remote mode, systemctl handles this.
    m_vmnode->stop_process();
    string params = mxb::string_printf("--config=%s", m_cnf_path.c_str());
    return m_vmnode->start_process(params) ? 0 : 1;
}

int MaxScale::stop_maxscale()
{
    int res;
    if (m_use_valgrind)
    {
        const char kill_vgrind[] = "kill $(pidof valgrind) 2>&1 > /dev/null";
        res = ssh_node(kill_vgrind, true);
        auto vgrind_pid = ssh_output("pidof valgrind");
        bool still_running = (atoi(vgrind_pid.output.c_str()) > 0);
        if ((res != 0) || still_running)
        {
            // Try again, maybe it will work.
            res = ssh_node(kill_vgrind, true);
        }
    }
    else
    {
        res = m_vmnode->stop_process() ? 0 : 1;
    }
    return res;
}

long unsigned MaxScale::get_maxscale_memsize(int m)
{
    auto res = ssh_output("ps -e -o pid,vsz,comm= | grep maxscale", false);
    long unsigned mem = 0;
    pid_t pid;
    sscanf(res.output.c_str(), "%d %lu", &pid, &mem);
    return mem;
}

int MaxScale::port(enum service type) const
{
    switch (type)
    {
    case RWSPLIT:
        return rwsplit_port;

    case READCONN_MASTER:
        return readconn_master_port;

    case READCONN_SLAVE:
        return readconn_slave_port;
    }
    return -1;
}

void MaxScale::wait_for_monitor(int intervals)
{
    const string path = "maxscale/debug/monitor_wait";
    for (int i = 0; i < intervals; i++)
    {
        auto res = curl_rest_api(path);
        if (res.rc)
        {
            log().add_failure("Monitor wait failed. Error %i, %s", res.rc, res.output.c_str());
            break;
        }
        else if (!res.output.empty())
        {
            mxb::Json result;
            if (result.load_string(res.output))
            {
                auto errors = result.get_array_elems("errors");
                if (!errors.empty())
                {
                    string err_msg = errors[0].get_string("detail");
                    log().add_failure("Monitor wait failed. %s", err_msg.c_str());
                    break;
                }
            }
            else
            {
                log().add_failure("Could not parse output of %s to json.", path.c_str());
                break;
            }
        }
    }
}

void MaxScale::sleep_and_wait_for_monitor(int sleep_s, int intervals)
{
    sleep(sleep_s);
    wait_for_monitor(intervals);
}

const char* MaxScale::ip() const
{
    return m_use_ipv6 ? m_vmnode->ip6s().c_str() : m_vmnode->ip4();
}

const char* MaxScale::ip_private() const
{
    return m_vmnode->priv_ip();
}

void MaxScale::set_use_ipv6(bool use_ipv6)
{
    m_use_ipv6 = use_ipv6;
}

void MaxScale::set_ssl(bool ssl)
{
    m_ssl = ssl;
}

const char* MaxScale::hostname() const
{
    return m_vmnode->hostname();
}

const char* MaxScale::access_user() const
{
    return m_vmnode->access_user();
}

const char* MaxScale::access_homedir() const
{
    return m_vmnode->access_homedir();
}

const char* MaxScale::access_sudo() const
{
    return m_vmnode->access_sudo();
}

const char* MaxScale::sshkey() const
{
    return m_vmnode->sshkey();
}

const std::string& MaxScale::prefix()
{
    return my_prefix;
}

const char* MaxScale::ip4() const
{
    return m_vmnode->ip4();
}

const std::string& MaxScale::node_name() const
{
    return m_vmnode->m_name;
}

mxt::CmdResult MaxScale::maxctrl(const std::string& cmd, bool sudo)
{
    string total_cmd;
    if (m_vmnode->is_remote())
    {
        total_cmd = mxb::string_printf("maxctrl %s 2>&1", cmd.c_str());
    }
    else
    {
        total_cmd = mxb::string_printf("%s %s 2>&1", m_local_maxctrl.c_str(), cmd.c_str());
    }
    return m_vmnode->run_cmd_output(total_cmd);
}

mxt::CmdResult MaxScale::maxctrlf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    auto rval = vmaxctrl(Expect::SUCCESS, format, args);
    va_end(args);
    return rval;
}

mxt::CmdResult MaxScale::maxctrlf(MaxScale::Expect expect, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    auto rval = vmaxctrl(expect, fmt, args);
    va_end(args);
    return rval;
}

mxt::CmdResult MaxScale::vmaxctrl(MaxScale::Expect expect, const char* format, va_list args)
{
    string cmd = mxb::string_vprintf(format, args);
    auto res = maxctrl(cmd, false);
    if (expect == Expect::SUCCESS)
    {
        log().expect(!res.rc, "MaxCtrl command '%s' failed: %s", cmd.c_str(), res.output.c_str());
    }
    else if (expect == Expect::FAIL)
    {
        log().expect(res.rc, "MaxCtrl command '%s' succeeded when failure was expected", cmd.c_str());
    }
    else if (res.rc)
    {
        // Report error but don't classify it as a test error.
        log().log_msgf("MaxCtrl command '%s' failed: %s", cmd.c_str(), res.output.c_str());
    }
    return res;
}

bool MaxScale::use_valgrind() const
{
    return m_use_valgrind;
}

int MaxScale::restart()
{
    return restart_maxscale();
}


void MaxScale::start()
{
    int res = start_maxscale();
    log().expect(res == 0, "MaxScale start failed, error %i.", res);
}

void MaxScale::stop()
{
    int res = stop_maxscale();
    log().expect(res == 0, "MaxScale stop failed, error %i.", res);
}

bool MaxScale::prepare_for_test()
{
    bool rval = false;
    if (m_vmnode->is_remote())
    {
        if (m_vmnode->init_connection())
        {
            if (m_use_valgrind)
            {
                auto vm = m_vmnode.get();
                vm->run_cmd_sudo("yum install -y valgrind gdb 2>&1");
                vm->run_cmd_sudo("apt install -y --force-yes valgrind gdb 2>&1");
                vm->run_cmd_sudo("zypper -n install valgrind gdb 2>&1");
                vm->run_cmd_sudo("rm -rf /var/cache/maxscale/maxscale.lock");
            }
            rval = true;
        }
    }
    else
    {
        rval = true;    // No preparations necessary in local mode, user is responsible for it.
    }
    return rval;
}

bool MaxScale::ssl() const
{
    return m_ssl;
}

mxt::Node& MaxScale::vm_node()
{
    return *m_vmnode;
}

void MaxScale::expect_running_status(bool expected)
{
    const int n_expected = expected ? 1 : 0;
    const int n_tries = 5;

    for (int i = 1; i <= n_tries; i++)
    {
        int n_mxs = get_n_running_processes();
        if (n_mxs == n_expected || n_mxs < 0)
        {
            break;
        }
        else if (i == n_tries)
        {
            log().add_failure("%i MaxScale processes detected when %i was expected.",
                              n_mxs, n_expected);
        }
        else
        {
            log().log_msgf("%i MaxScale processes detected when %i was expected. "
                           "Trying again in a second.",
                           n_mxs, n_expected);
            sleep(1);
        }
    }
}

int MaxScale::get_n_running_processes()
{
    const char* ps_cmd = m_use_valgrind ?
        "ps ax | grep valgrind | grep maxscale | grep -v grep | wc -l" :
        "ps -C maxscale | grep maxscale | wc -l";

    int rval = -1;
    auto cmd_res = ssh_output(ps_cmd, false);
    if (cmd_res.rc != 0)
    {
        log().add_failure("Can't check MaxScale running status. Command '%s' failed with code %i and "
                          "output '%s'.", ps_cmd, cmd_res.rc, cmd_res.output.c_str());
    }
    else if (cmd_res.output.empty())
    {
        log().add_failure("Can't check MaxScale running status. Command '%s' gave no output.", ps_cmd);
    }
    else
    {
        int num = 0;
        if (mxb::get_int(cmd_res.output, 10, &num))
        {
            rval = num;
        }
        else
        {
            log().add_failure("Unexpected output from '%s': %s", ps_cmd, cmd_res.output.c_str());
        }
    }
    return rval;
}

mxt::TestLogger& MaxScale::log() const
{
    return m_shared.log;
}

bool MaxScale::verbose() const
{
    return m_shared.settings.verbose;
}

bool MaxScale::start_and_check_started()
{
    int res = start_maxscale();
    expect_running_status(true);
    return res == 0;
}

bool MaxScale::stop_and_check_stopped()
{
    int res = stop_maxscale();
    expect_running_status(false);
    return res == 0;
}

bool MaxScale::reinstall(const std::string& target, const std::string& mdbci_config_name)
{
    bool rval = false;
    auto& vm = vm_node();
    log().log_msgf("Installing MaxScale on node %s.", vm.m_name.c_str());
    // TODO: make it via MDBCI and compatible with any distro
    vm.run_cmd_output_sudo("yum remove maxscale -y");
    vm.run_cmd_output_sudo("yum clean all");

    string install_cmd = mxb::string_printf(
        "mdbci install_product --product maxscale_ci --product-version %s %s/%s",
        target.c_str(), mdbci_config_name.c_str(), vm.m_name.c_str());
    if (m_shared.run_shell_command(install_cmd, "MaxScale install failed."))
    {
        rval = true;
    }
    return rval;
}

void MaxScale::copy_log(int mxs_ind, int timestamp, const std::string& test_name)
{
    string dest_log_dir;
    if (m_shared.settings.mdbci_test)
    {
        dest_log_dir = mxb::string_printf("%s/LOGS/%s", mxt::BUILD_DIR, test_name.c_str());
    }
    else
    {
        // When running test locally, save logs to the configured log storage directory.
        dest_log_dir = mxb::string_printf("%s/%s", m_log_storage_dir.c_str(), test_name.c_str());
    }

    // Copy main MaxScale logs to main test log directory, additional MaxScale logs (rare) to a subdirectory.
    if (timestamp != 0)
    {
        dest_log_dir.append(mxb::string_printf("/%04d", timestamp));
    }
    if (mxs_ind != 0)
    {
        dest_log_dir.append("/mxs").append(std::to_string(mxs_ind + 1));
    }

    string mkdir_cmd = mxb::string_printf("mkdir -p %s", dest_log_dir.c_str());
    m_shared.run_shell_command(mkdir_cmd, "");
    auto vm = m_vmnode.get();
    auto mxs_cnf_file = m_cnf_path.c_str();

    if (vm->is_remote())
    {
        string temp_logdir = mxb::string_printf("%s/logs", vm->access_homedir());
        const char* temp_logdirc = temp_logdir.c_str();
        int rc = ssh_node_f(true,
                            "rm -rf %s; mkdir %s;"
                            "cp %s/*.log %s/;"
                            "test -e /tmp/core* && cp /tmp/core* %s/ >& /dev/null;"
                            "cp %s %s/;"
                            "chmod 777 -R %s;"
                            "test -e /tmp/core*  && exit 42;"
                            "grep LeakSanitizer %s/* && exit 43;",
                            temp_logdirc, temp_logdirc,
                            m_log_dir.c_str(), temp_logdirc,
                            temp_logdirc,
                            mxs_cnf_file, temp_logdirc,
                            temp_logdirc, temp_logdirc);
        string log_source = mxb::string_printf("%s/*", temp_logdirc);
        vm->copy_from_node(log_source, dest_log_dir);
        log().expect(rc != 42, "Test should not generate core files");

        if (m_leak_check)
        {
            log().expect(rc != 43, "MaxScale should not leak memory");
        }
    }
    else
    {
        auto dest = dest_log_dir.c_str();
        m_shared.run_shell_cmdf("rm -rf %s/*", dest);
        m_shared.run_shell_cmdf("cp %s/*.log %s/", m_log_dir.c_str(), dest);
        m_shared.run_shell_cmdf("cp %s %s/", mxs_cnf_file, dest);
        // Ignore errors of next command, as core-files may not exist.
        string core_copy = mxb::string_printf("cp /tmp/core* %s/ 2>/dev/null", dest);
        system(core_copy.c_str());
    }
}

MYSQL* MaxScale::open_rwsplit_connection(const std::string& db)
{
    return open_conn(rwsplit_port, ip4(), m_user_name, m_password, m_ssl);
}

MaxScale::SMariaDB MaxScale::try_open_rwsplit_connection(const string& db)
{
    return try_open_rwsplit_connection(SslMode::AUTO, m_user_name, m_password, db);
}

MaxScale::SMariaDB MaxScale::try_open_rwsplit_connection(const string& user, const string& pass,
                                                         const string& db)
{
    return try_open_rwsplit_connection(SslMode::AUTO, user, pass, db);
}

MaxScale::SMariaDB MaxScale::try_open_rwsplit_connection(MaxScale::SslMode ssl, const string& user,
                                                         const std::string& pass, const string& db)
{
    return try_open_connection(ssl, rwsplit_port, user, pass, db);
}

MaxScale::SMariaDB
MaxScale::try_open_connection(MaxScale::SslMode ssl, int port, const string& user, const string& pass,
                              const string& db)
{
    auto conn = std::make_unique<mxt::MariaDB>(log());
    auto& sett = conn->connection_settings();
    sett.user = user;
    sett.password = pass;
    if (ssl == SslMode::ON || (ssl == SslMode::AUTO && m_ssl))
    {
        auto base_dir = mxt::SOURCE_DIR;
        sett.ssl.key = mxb::string_printf("%s/ssl-cert/client.key", base_dir);
        sett.ssl.cert = mxb::string_printf("%s/ssl-cert/client.crt", base_dir);
        sett.ssl.ca = mxb::string_printf("%s/ssl-cert/ca.crt", base_dir);
        sett.ssl.enabled = true;
    }

    conn->try_open(ip(), port, db);
    return conn;
}

MaxScale::SMariaDB
MaxScale::try_open_connection(int port, const string& user, const string& pass, const string& db)
{
    return try_open_connection(SslMode::AUTO, port, user, pass, db);
}

std::unique_ptr<mxt::MariaDB> MaxScale::open_rwsplit_connection2(const string& db)
{
    auto conn = try_open_rwsplit_connection(db);
    m_shared.log.expect(conn->is_open(), "Failed to open MySQL connection to RWSplit.");
    return conn;
}

MaxScale::SMariaDB MaxScale::open_rwsplit_connection2_nodb()
{
    return open_rwsplit_connection2("");
}

Connection MaxScale::rwsplit(const std::string& db)
{
    return Connection(ip4(), rwsplit_port, m_user_name, m_password, db, m_ssl);
}

Connection MaxScale::get_connection(int port, const std::string& db)
{
    return Connection(ip4(), port, m_user_name, m_password, db, m_ssl);
}

MYSQL* MaxScale::open_readconn_master_connection()
{
    return open_conn(readconn_master_port, ip4(), m_user_name, m_password, m_ssl);
}

Connection MaxScale::readconn_master(const std::string& db)
{
    return Connection(ip4(), readconn_master_port, m_user_name, m_password, db, m_ssl);
}

MYSQL* MaxScale::open_readconn_slave_connection()
{
    return open_conn(readconn_slave_port, ip4(), m_user_name, m_password, m_ssl);
}

Connection MaxScale::readconn_slave(const std::string& db)
{
    return Connection(ip4(), readconn_slave_port, m_user_name, m_password, db, m_ssl);
}

void MaxScale::close_rwsplit()
{
    mysql_close(conn_rwsplit);
    conn_rwsplit = NULL;
}

void MaxScale::close_readconn_master()
{
    mysql_close(conn_master);
    conn_master = NULL;
}

int MaxScale::ssh_node_f(bool sudo, const char* format, ...)
{
    va_list valist;
    va_start(valist, format);
    string cmd = mxb::string_vprintf(format, valist);
    va_end(valist);
    return ssh_node(cmd, sudo);
}

void MaxScale::copy_fw_rules(const std::string& rules_name, const std::string& rules_dir)
{
    ssh_node_f(true, "cd %s; rm -rf rules; mkdir rules; chown %s:%s rules",
               access_homedir(), access_user(), access_user());

    string src = rules_dir + "/" + rules_name;
    string dest = string(access_homedir()) + "/rules/rules.txt";

    copy_to_node(src.c_str(), dest.c_str());
    ssh_node_f(true, "chmod a+r %s", dest.c_str());
}

bool MaxScale::log_matches(std::string pattern) const
{
    // Replace single quotes with wildcard characters, should solve most problems
    for (auto& a : pattern)
    {
        if (a == '\'')
        {
            a = '.';
        }
    }

    MaxScale* p = const_cast<MaxScale*>(this);

    return p->ssh_node_f(true, "grep '%s' %s/maxscale*.log", pattern.c_str(), m_log_dir.c_str()) == 0;
}

mxt::CmdResult MaxScale::ssh_output(const std::string& cmd, bool sudo)
{
    using CmdPriv = mxt::VMNode::CmdPriv;
    return m_vmnode->run_cmd_output(cmd, sudo ? CmdPriv::SUDO : CmdPriv::NORMAL);
}

bool MaxScale::copy_to_node(const char* src, const char* dest)
{
    return m_vmnode->copy_to_node(src, dest);
}

bool MaxScale::copy_from_node(const char* src, const char* dest)
{
    return m_vmnode->copy_from_node(src, dest);
}

void MaxScale::write_env_vars()
{
    m_vmnode->write_node_env_vars();
}

int MaxScale::ssh_node(const string& cmd, bool sudo)
{
    using CmdPriv = mxt::VMNode::CmdPriv;
    return m_vmnode->run_cmd(cmd, sudo ? CmdPriv::SUDO : CmdPriv::NORMAL);
}

void MaxScale::check_servers_status(const std::vector<mxt::ServerInfo::bitfield>& expected_status)
{
    auto data = get_servers();
    data.check_servers_status(expected_status);
}

void MaxScale::check_print_servers_status(const std::vector<uint32_t>& expected_status)
{
    auto data = get_servers();
    data.print();
    data.check_servers_status(expected_status);
}

void MaxScale::alter_monitor(const string& mon_name, const string& setting, const string& value)
{
    string cmd = mxb::string_printf("alter monitor %s %s %s", mon_name.c_str(),
                                    setting.c_str(), value.c_str());
    auto res = maxctrl(cmd);
    log().expect(res.rc == 0 && res.output == "OK", "Alter monitor command '%s' failed.", cmd.c_str());
}

void MaxScale::alter_service(const string& svc_name, const string& setting, const string& value)
{
    string cmd = mxb::string_printf("alter service %s %s %s", svc_name.c_str(),
                                    setting.c_str(), value.c_str());
    auto res = maxctrl(cmd);
    log().expect(res.rc == 0 && res.output == "OK", "Alter service command '%s' failed.", cmd.c_str());
}

void MaxScale::alter_server(const string& srv_name, const string& setting, const string& value)
{
    string cmd = mxb::string_printf("alter server %s %s %s", srv_name.c_str(),
                                    setting.c_str(), value.c_str());
    auto res = maxctrl(cmd);
    log().expect(res.rc == 0 && res.output == "OK", "Alter server command '%s' failed.", cmd.c_str());
}

void MaxScale::delete_log()
{
    auto cmd = mxb::string_printf("truncate -s 0 %s/maxscale.log", m_log_dir.c_str());
    auto res = vm_node().run_cmd_output(cmd, mxt::VMNode::CmdPriv::SUDO);
    log().expect(res.rc == 0, "'%s' failed", cmd.c_str());
}

mxt::CmdResult MaxScale::curl_rest_api(const std::string& path)
{
    string cmd = mxb::string_printf("curl --silent --show-error http://%s:%s@%s:%s/v1/%s",
                                    m_rest_user.c_str(), m_rest_pw.c_str(),
                                    m_rest_ip.c_str(), m_rest_port.c_str(),
                                    path.c_str());
    return m_vmnode->run_cmd_output(cmd, Node::CmdPriv::NORMAL);
}

mxt::ServersInfo MaxScale::get_servers()
{
    using mxt::ServerInfo;
    using mxt::ServersInfo;
    using mxb::Json;

    const string field_servers = "servers";
    const string field_data = "data";
    const string field_id = "id";
    const string field_attr = "attributes";
    const string field_state = "state";
    const string field_state_details = "state_details";
    const string field_mgroup = "master_group";
    const string field_rlag = "replication_lag";
    const string field_serverid = "server_id";
    const string field_readonly = "read_only";
    const string field_slave_conns = "slave_connections";
    const string field_statistics = "statistics";
    const string field_gtid = "gtid_current_pos";

    // Slave conn fields
    const string field_scon_name = "connection_name";
    const string field_scon_gtid = "gtid_io_pos";
    const string field_scon_id = "master_server_id";
    const string field_scon_io = "slave_io_running";
    const string field_scon_sql = "slave_sql_running";

    // Statistics fields
    const string field_pers_conns = "persistent_connections";
    const string field_connections = "connections";

    // Parameters
    const string field_parameters = "parameters";
    const string field_ssl = "ssl";

    auto try_get_int = [](const Json& json, const string& key, int64_t failval) {
        int64_t rval = failval;
        json.try_get_int(key, &rval);
        return rval;
    };

    auto try_get_bool = [](const Json& json, const string& key, bool failval) {
        bool rval = failval;
        json.try_get_bool(key, &rval);
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
                string state_details;
                attr.try_get_string(field_state_details, &state_details);
                if (!info.status_from_string(state, state_details))
                {
                    log().add_failure("Server status string parsing error. State: '%s', details: '%s'",
                                      state.c_str(), state_details.c_str());
                }

                // The following depend on the monitor and may be null.
                info.master_group = try_get_int(attr, field_mgroup, ServerInfo::GROUP_NONE);
                info.rlag = try_get_int(attr, field_rlag, ServerInfo::RLAG_NONE);
                info.server_id = try_get_int(attr, field_serverid, ServerInfo::SRV_ID_NONE);
                info.read_only = try_get_bool(attr, field_readonly, false);
                attr.try_get_string(field_gtid, &info.gtid);

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
                info.connections = try_get_int(stats, field_connections, 0);

                auto params = attr.get_object(field_parameters);
                info.ssl_configured = try_get_bool(params, field_ssl, false);
                rval.add(info);
            }
        }
        else
        {
            log().add_failure("Invalid data from REST-API servers query: %s", all.error_msg().c_str());
        }
    }
    else
    {
        log().add_failure("REST-API servers query failed. Error %i, %s", res.rc, mxb_strerror(res.rc));
    }
    return rval;
}

const std::string& MaxScale::user_name() const
{
    return m_user_name;
}

const std::string& MaxScale::password() const
{
    return m_password;
}

const std::string& MaxScale::cnf_path() const
{
    return m_cnf_path;
}

int MaxScale::get_master_server_id()
{
    return get_servers().get_master().server_id;
}

void MaxScale::write_in_log(string&& str)
{
    char* buf = str.data();
    while (char* c = strchr(buf, '\''))
    {
        *c = '^';
    }
    // Assuming here that if running MaxScale locally, the user has write access to MaxScale log.
    ssh_node_f(m_vmnode->is_remote(), "echo '--- %s ---' >> %s/maxscale.log", buf, m_log_dir.c_str());
}

void MaxScale::delete_logs_and_rtfiles()
{
    if (m_vmnode->is_remote())
    {
        ssh_node_f(true,
                   "iptables -F INPUT;"
                   "rm -rf %s/*.log /tmp/core* /dev/shm/* /var/lib/maxscale/* /var/lib/maxscale/.secrets;"
                   "find /var/*/maxscale -name 'maxscale.lock' -delete;",
                   m_log_dir.c_str());
    }
    else
    {
        // MaxScale running locally, delete any old logs and runtime config files.
        // TODO: make datadir configurable.
        m_shared.run_shell_cmdf("rm -rf %s/*.log  /tmp/core* /var/lib/maxscale/maxscale.cnf.d/*",
                                m_log_dir.c_str());
    }
}

void MaxScale::create_report()
{
    // Create report and save it to MaxScale log dir. It will get copied along with other logs.
    string cmd = mxb::string_printf("create report %s/maxctrl-report.log", m_log_dir.c_str());
    maxctrl("create report /var/log/maxscale/maxctrl-report.log");
}

void MaxScale::set_log_dir(string&& str)
{
    // The log directory is used "rm -rf"-style commands. Check that dir is not empty to avoid
    // an accidental "rm -rf /*".
    if (str.length() >= 2 && str[0] == '/' && str.find_first_not_of('/', 1) != string::npos)
    {
        m_log_dir = std::move(str);
    }
    else
    {
        log().add_failure("MaxScale log path '%s' is invalid.", str.c_str());
    }
}

std::string MaxScale::cert_path() const
{
    return mxb::string_printf("%s/certs/mxs.crt", access_homedir());
}

std::string MaxScale::cert_key_path() const
{
    return mxb::string_printf("%s/certs/mxs.key", access_homedir());
}

std::string MaxScale::ca_cert_path() const
{
    return mxb::string_printf("%s/certs/ca.crt", access_homedir());
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

ServerInfo ServersInfo::get(const std::string& cnf_name) const
{
    ServerInfo rval;
    for (auto& elem : m_servers)
    {
        if (elem.name == cnf_name)
        {
            rval = elem;
            break;
        }
    }
    return rval;
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

void ServersInfo::check_connections(const std::vector<int>& expected_conns)
{
    auto tester = [&](size_t i) {
        auto expected = expected_conns[i];
        auto& info = m_servers[i];
        if (expected != info.connections)
        {
            m_log->add_failure("Wrong number of connections for %s. Got '%li', expected '%i'.",
                               info.name.c_str(), info.connections, expected);
        }
    };
    check_servers_property(expected_conns.size(), tester);
}

void ServersInfo::check_read_only(const std::vector<bool>& expected_ro)
{
    auto tester = [&](size_t i) {
        auto expected = expected_ro[i];
        auto& info = m_servers[i];
        if (expected != info.read_only)
        {
            m_log->add_failure("Wrong read_only for %s. Got '%i', expected '%i'.",
                               info.name.c_str(), info.read_only, expected);
        }
    };
    check_servers_property(expected_ro.size(), tester);
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

void ServersInfo::print()
{
    if (m_servers.empty())
    {
        m_log->log_msgf("No server info received from REST api.");
    }
    else
    {
        string total_msg;
        auto n = m_servers.size();
        total_msg.reserve(n * 30);
        total_msg += "Server information from REST api:\n";
        for (auto& elem : m_servers)
        {
            total_msg.append(elem.to_string_short()).append("\n");
        }
        m_log->log_msg(total_msg);
    }
}

const std::vector<ServerInfo::bitfield>& ServersInfo::default_repl_states()
{
    static const std::vector<mxt::ServerInfo::bitfield> def_repl_states =
    {mxt::ServerInfo::master_st,
     mxt::ServerInfo::slave_st, mxt::ServerInfo::slave_st, mxt::ServerInfo::slave_st};
    return def_repl_states;
}

ServersInfo::RoleInfo ServersInfo::get_role_info() const
{
    RoleInfo rval;

    for (const auto& srv : m_servers)
    {
        auto status = srv.status;
        if (status == mxt::ServerInfo::master_st)
        {
            rval.masters++;
            if (rval.master_name.empty())
            {
                rval.master_name = srv.name;
            }
        }
        else if (status == mxt::ServerInfo::slave_st)
        {
            rval.slaves++;
        }
        else if (status == mxt::ServerInfo::RUNNING)
        {
            rval.running++;
        }
    }

    return rval;
}

std::vector<ServerInfo>::iterator ServersInfo::begin()
{
    return m_servers.begin();
}

std::vector<ServerInfo>::iterator ServersInfo::end()
{
    return m_servers.end();
}

bool ServerInfo::status_from_string(const string& source, const string& details)
{
    status = UNKNOWN;
    bool error = false;

    auto check_tokens = [this, &error](std::vector<string> tokens, StatusType expected_type) {
        const char* expected_type_str = (expected_type == StatusType::STATUS) ? "status" : "detail";

        for (string& token : tokens)
        {
            mxb::trim(token);
            // Expect all flags to be recognized and be correct type (status or detail).
            bool found = false;
            for (const auto& elem : status_flag_to_str)
            {
                if (elem.desc == token)
                {
                    if (elem.type == expected_type)
                    {
                        status |= elem.bit;
                    }
                    else
                    {
                        printf("Unexpected flag type for '%s', expected %s.\n",
                               token.c_str(), expected_type_str);
                        error = true;
                    }
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                printf("Unrecognized %s flag '%s'\n", expected_type_str, token.c_str());
                error = true;
            }
        }
    };

    auto status_tokens = mxb::strtok(source, ",");
    check_tokens(std::move(status_tokens), StatusType::STATUS);

    if (!details.empty())
    {
        auto details_tokens = mxb::strtok(details, ",");
        check_tokens(std::move(details_tokens), StatusType::DETAIL);
    }
    return !error;
}

std::string ServerInfo::status_to_string(bitfield status)
{
    if (status == mxt::ServerInfo::UNKNOWN)
    {
        return "Unknown";
    }

    string rval;
    string sep;

    while (status)
    {
        bool found = false;

        for (const auto& elem : status_flag_to_str)
        {
            if (elem.bit & status)
            {
                rval.append(sep).append(elem.desc);
                sep = ", ";
                status &= ~elem.bit;
                found = true;
                break;
            }
        }

        if (!found)
        {
            mxb_assert(!true);      // Unrecognized test status bit.
            break;
        }
    }
    return rval;
}

std::string ServerInfo::status_to_string() const
{
    return status_to_string(status);
}

std::string ServerInfo::to_string_short() const
{
    return mxb::string_printf("%10s, %15s, %s", name.c_str(), status_to_string().c_str(), gtid.c_str());
}
}

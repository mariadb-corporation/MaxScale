#include <maxtest/maxscales.hh>
#include <string>
#include <maxbase/format.hh>
#include <maxbase/jansson.h>
#include <maxtest/json.hh>
#include <maxbase/string.hh>
#include <maxtest/envv.hh>
#include <maxtest/log.hh>
#include <maxtest/mariadb_connector.hh>
#include <maxtest/testconnections.hh>


using std::string;

namespace
{
const string my_prefix = "maxscale";
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
    user_name = envvar_get_set(key_user.c_str(), "skysql");

    string key_pw = mxb::string_printf("%s_password", prefixc);
    password = envvar_get_set(key_pw.c_str(), "skysql");

    m_use_valgrind = readenv_bool("use_valgrind", false);
    m_use_callgrind = readenv_bool("use_callgrind", false);
    if (m_use_callgrind)
    {
        m_use_valgrind = true;
    }

    m_vmnode = nullptr;
    bool rval = false;

    auto new_node = std::make_unique<mxt::VMNode>(m_shared, vm_name);
    if (new_node->configure(nwconfig))
    {
        m_vmnode = move(new_node);

        string key_cnf = vm_name + "_cnf";
        maxscale_cnf = envvar_get_set(key_cnf.c_str(), "/etc/maxscale.cnf");

        string key_log_dir = vm_name + "_log_dir";
        maxscale_log_dir = envvar_get_set(key_log_dir.c_str(), "/var/log/maxscale/");

        string key_binlog_dir = vm_name + "_binlog_dir";
        m_binlog_dir = envvar_get_set(key_binlog_dir.c_str(), "/var/lib/maxscale/Binlog_Service/");

        rwsplit_port = 4006;
        readconn_master_port = 4008;
        readconn_slave_port = 4009;

        ports[0] = rwsplit_port;
        ports[1] = readconn_master_port;
        ports[2] = readconn_slave_port;

        rval = true;
    }
    return rval;
}

int MaxScale::connect_rwsplit(const std::string& db)
{
    mysql_close(conn_rwsplit[0]);

    conn_rwsplit[0] = open_conn_db(rwsplit_port, ip(), db, user_name, password, m_ssl);
    routers[0] = conn_rwsplit[0];

    int rc = 0;
    int my_errno = mysql_errno(conn_rwsplit[0]);

    if (my_errno)
    {
        if (verbose())
        {
            printf("Failed to connect to readwritesplit: %d, %s\n", my_errno, mysql_error(conn_rwsplit[0]));
        }
        rc = my_errno;
    }

    return rc;
}

int MaxScale::connect_readconn_master(const std::string& db)
{
    MYSQL*& conn_rc_master = conn_master;
    mysql_close(conn_rc_master);

    conn_rc_master = open_conn_db(readconn_master_port, ip(), db, user_name, password, m_ssl);
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

    conn_rc_slave = open_conn_db(readconn_slave_port, ip(), db, user_name, password, m_ssl);
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
        res = ssh_output("systemctl restart maxscale", true).rc;
    }
    return res;
}

int MaxScale::start_maxscale()
{
    int res;
    if (m_use_valgrind)
    {
        auto log_dir = maxscale_log_dir.c_str();
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
        res = ssh_output("systemctl restart maxscale", true).rc;
    }
    return res;
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
        res = ssh_output("systemctl stop maxscale", true).rc;
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

StringSet MaxScale::get_server_status(const std::string& name)
{
    StringSet rval;
    auto res = maxctrl("api get servers/" + name + " data.attributes.state");

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
    for (int i = 0; i < intervals; i++)
    {
        auto res = curl_rest_api("maxscale/debug/monitor_wait");
        if (res.rc)
        {
            log().add_failure("Monitor wait failed. Error %i, %s", res.rc, res.output.c_str());
            break;
        }
    }
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
    using CmdPriv = mxt::VMNode::CmdPriv;
    return m_vmnode->run_cmd_output("maxctrl " + cmd, sudo ? CmdPriv::SUDO : CmdPriv::NORMAL);
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
    if (m_shared.settings.local_maxscale)
    {
        // MaxScale is running locally, overwrite node address.
        m_vmnode->set_local();
    }

    bool rval = false;
    if (m_vmnode->init_ssh_master())
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
    return rval;
}

bool MaxScale::ssl() const
{
    return m_ssl;
}

mxt::VMNode& MaxScale::vm_node()
{
    return *m_vmnode;
}

void MaxScale::expect_running_status(bool expected)
{
    const char* ps_cmd = m_use_valgrind ?
        "ps ax | grep valgrind | grep maxscale | grep -v grep | wc -l" :
        "ps -C maxscale | grep maxscale | wc -l";

    auto cmd_res = ssh_output(ps_cmd, false);
    if (cmd_res.output.empty() || (cmd_res.rc != 0))
    {
        log().add_failure("Can't check MaxScale running status. Command '%s' failed with code %i and "
                          "output '%s'.", ps_cmd, cmd_res.rc, cmd_res.output.c_str());
        return;
    }

    cmd_res.output = mxt::cutoff_string(cmd_res.output, '\n');
    string expected_str = expected ? "1" : "0";

    if (cmd_res.output != expected_str)
    {
        log().log_msgf("%s MaxScale processes detected when %s was expected. Trying again in 5 seconds.",
                       cmd_res.output.c_str(), expected_str.c_str());
        sleep(5);
        cmd_res = ssh_output(ps_cmd, false);
        cmd_res.output = mxt::cutoff_string(cmd_res.output, '\n');

        if (cmd_res.output != expected_str)
        {
            log().add_failure("%s MaxScale processes detected when %s was expected.",
                              cmd_res.output.c_str(), expected_str.c_str());
        }
    }
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
    string log_dir;
    if (timestamp == 0)
    {
        log_dir = mxb::string_printf("%s/LOGS/%s", mxt::BUILD_DIR, test_name.c_str());
    }
    else
    {
        log_dir = mxb::string_printf("%s/LOGS/%s/%04d", mxt::BUILD_DIR, test_name.c_str(), timestamp);
    }

    string dest_log_dir = mxb::string_printf("%s/%03d", log_dir.c_str(), mxs_ind);
    string sys = "mkdir -p " + dest_log_dir;
    system(sys.c_str());

    auto vm = m_vmnode.get();
    auto mxs_logdir = maxscale_log_dir.c_str();
    auto mxs_cnf_file = maxscale_cnf.c_str();

    if (vm->is_remote())
    {
        auto homedir = vm->access_homedir();
        int rc = ssh_node_f(true,
                            "rm -rf %s/logs; mkdir %s/logs;"
                            "cp %s/*.log %s/logs/;"
                            "test -e /tmp/core* && cp /tmp/core* %s/logs/ >& /dev/null;"
                            "cp %s %s/logs/;"
                            "chmod 777 -R %s/logs;"
                            "test -e /tmp/core*  && exit 42;",
                            homedir, homedir,
                            mxs_logdir, homedir,
                            homedir,
                            mxs_cnf_file, homedir,
                            homedir);
        string log_source = mxb::string_printf("%s/logs/*", homedir);
        vm->copy_from_node(log_source, dest_log_dir);
        log().expect(rc != 42, "Test should not generate core files");
    }
    else
    {
        auto dest = dest_log_dir.c_str();
        ssh_node_f(true, "cp %s/*.logs %s/", mxs_logdir, dest);
        ssh_node_f(true, "cp /tmp/core* %s/", dest);
        ssh_node_f(true, "cp %s %s/", mxs_cnf_file, dest);
        ssh_node_f(true, "chmod a+r -R %s", dest);
    }
}

MYSQL* MaxScale::open_rwsplit_connection(const std::string& db)
{
    return open_conn(rwsplit_port, ip4(), user_name, password, m_ssl);
}

std::unique_ptr<mxt::MariaDB> MaxScale::open_rwsplit_connection2(const string& db)
{
    auto conn = std::make_unique<mxt::MariaDB>(log());
    auto& sett = conn->connection_settings();
    sett.user = user_name;
    sett.password = password;
    if (m_ssl)
    {
        auto base_dir = mxt::SOURCE_DIR;
        sett.ssl.key = mxb::string_printf("%s/ssl-cert/client-key.pem", base_dir);
        sett.ssl.cert = mxb::string_printf("%s/ssl-cert/client-cert.pem", base_dir);
        sett.ssl.ca = mxb::string_printf("%s/ssl-cert/ca.pem", base_dir);
    }

    conn->open(ip(), rwsplit_port, db);
    return conn;
}

Connection MaxScale::rwsplit(const std::string& db)
{
    return Connection(ip4(), rwsplit_port, user_name, password, db, m_ssl);
}

Connection MaxScale::get_connection(int port, const std::string& db)
{
    return Connection(ip4(), port, user_name, password, db, m_ssl);
}

MYSQL* MaxScale::open_readconn_master_connection()
{
    return open_conn(readconn_master_port, ip4(), user_name, password, m_ssl);
}

Connection MaxScale::readconn_master(const std::string& db)
{
    return Connection(ip4(), readconn_master_port, user_name, password, db, m_ssl);
}

MYSQL* MaxScale::open_readconn_slave_connection()
{
    return open_conn(readconn_slave_port, ip4(), user_name, password, m_ssl);
}

Connection MaxScale::readconn_slave(const std::string& db)
{
    return Connection(ip4(), readconn_slave_port, user_name, password, db, m_ssl);
}

void MaxScale::close_rwsplit()
{
    mysql_close(conn_rwsplit[0]);
    conn_rwsplit[0] = NULL;
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

void MaxScale::delete_log()
{
    vm_node().run_cmd_output("truncate -s 0 /var/log/maxscale/maxscale.log",
                             mxt::VMNode::CmdPriv::SUDO);
}

mxt::CmdResult MaxScale::curl_rest_api(const std::string& path)
{
    string cmd = mxb::string_printf("curl --silent --show-error http://%s:%s@%s:%s/v1/%s",
                                    m_rest_user.c_str(), m_rest_pw.c_str(),
                                    m_rest_ip.c_str(), m_rest_port.c_str(),
                                    path.c_str());
    return ssh_output(cmd, true);
}

mxt::ServersInfo MaxScale::get_servers()
{
    using mxt::ServerInfo;
    using mxt::ServersInfo;

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
                info.status_from_string(state);

                // The following depend on the monitor and may be null.
                info.master_group = try_get_int(attr, field_mgroup, ServerInfo::GROUP_NONE);
                info.rlag = try_get_int(attr, field_rlag, ServerInfo::RLAG_NONE);
                info.server_id = try_get_int(attr, field_serverid, ServerInfo::SRV_ID_NONE);
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

void ServerInfo::status_from_string(const string& source)
{
    auto flags = mxb::strtok(source, ",");
    status = UNKNOWN;
    for (string& flag : flags)
    {
        mxb::trim(flag);
        if (flag == "Down")
        {
            status |= DOWN;
        }
        else if (flag == "Running")
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
        else if (flag == "Maintenance")
        {
            status |= MAINT;
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
    std::string rval = "Unknown";
    if (status)
    {
        std::vector<string> items;
        items.reserve(2);
        if (status & DOWN)
        {
            items.emplace_back("Down");
        }
        if (status & MASTER)
        {
            items.emplace_back("Master");
        }
        if (status & SLAVE)
        {
            items.emplace_back("Slave");
        }
        if (status & MAINT)
        {
            items.emplace_back("Maintenance");
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

std::string ServerInfo::to_string_short() const
{
    return mxb::string_printf("%10s, %15s, %s", name.c_str(), status_to_string().c_str(), gtid.c_str());
}
}

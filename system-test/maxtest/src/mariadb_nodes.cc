/**
 * @file mariadb_nodes.cpp - backend nodes routines
 *
 * @verbatim
 * Revision History
 *
 * Date     Who     Description
 * 17/11/14 Timofey Turenko Initial implementation
 *
 * @endverbatim
 */

#include <maxtest/mariadb_nodes.hh>
#include <climits>
#include <string>
#include <sstream>
#include <iostream>
#include <vector>
#include <future>
#include <functional>
#include <algorithm>
#include <maxbase/format.hh>
#include <maxtest/envv.hh>
#include <maxtest/log.hh>
#include <maxtest/test_dir.hh>
#include <cassert>

using std::cout;
using std::endl;
using std::string;

namespace
{
bool g_require_gtid = false;

const string type_columnstore = "columnstore";
}

void MariaDBCluster::require_gtid(bool value)
{
    g_require_gtid = value;
}

bool MariaDBCluster::get_require_gtid()
{
    return g_require_gtid;
}

MariaDBCluster::MariaDBCluster(mxt::SharedData* shared, const std::string& nwconf_prefix,
                               const std::string& cnf_server_prefix)
    : Nodes(shared)
    , m_cnf_server_name(cnf_server_prefix)
    , m_prefix(nwconf_prefix)
{
    m_test_dir = test_dir;
}

bool MariaDBCluster::setup(const mxt::NetworkConfig& nwconfig, int n_min_expected)
{
    bool rval = false;
    int found = read_nodes_info(nwconfig);
    if (found < n_min_expected)
    {
        m_shared.log.add_failure("Found %i node(s) in network_config when at least %i was expected.",
                                 found, n_min_expected);
    }
    else
    {
        if (Nodes::init_ssh_masters())
        {
            truncate_mariadb_logs();
            prepare_for_test();
            close_active_connections();
            rval = true;
        }
    }
    return rval;
}

MariaDBCluster::~MariaDBCluster()
{
    for (int i = 0; i < N; i++)
    {
        if (m_blocked[i])
        {
            unblock_node(i);
        }
    }

    close_connections();
}

int MariaDBCluster::connect(int i, const std::string& db)
{
    if (nodes[i] == NULL || mysql_ping(nodes[i]) != 0)
    {
        if (nodes[i])
        {
            mysql_close(nodes[i]);
        }
        nodes[i] = open_conn_db_timeout(port[i], ip4(i), db.c_str(), user_name, password, 50, ssl);
    }

    if ((nodes[i] == NULL) || (mysql_errno(nodes[i]) != 0))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

int MariaDBCluster::connect(const std::string& db)
{
    int res = 0;

    for (int i = 0; i < N; i++)
    {
        res += connect(i, db);
    }

    return res;
}

bool MariaDBCluster::robust_connect(int n)
{
    bool rval = false;

    for (int i = 0; i < n; i++)
    {
        if (connect() == 0)
        {
            // Connected successfully, return immediately
            rval = true;
            break;
        }

        // We failed to connect, disconnect and wait for a second before trying again
        disconnect();
        sleep(1);
    }

    return rval;
}

void MariaDBCluster::close_connections()
{
    for (int i = 0; i < N; i++)
    {
        if (nodes[i] != NULL)
        {
            mysql_close(nodes[i]);
            nodes[i] = NULL;
        }
    }
}

int MariaDBCluster::read_nodes_info(const mxt::NetworkConfig& nwconfig)
{
    auto prefixc = m_prefix.c_str();

    string key_user = mxb::string_printf("%s_user", prefixc);
    user_name = envvar_get_set(key_user.c_str(), "skysql");

    string key_pw = mxb::string_printf("%s_password", prefixc);
    password = envvar_get_set(key_pw.c_str(), "skysql");

    string key_ssl = mxb::string_printf("%s_ssl", prefixc);
    ssl = readenv_bool(key_ssl.c_str(), false);

    const string space = " ";
    const char start_db_def[] = "systemctl start mariadb || service mysql start";
    const char stop_db_def[] = "systemctl stop mariadb || service mysql stop";
    const char clean_db_def[] = "rm -rf /var/lib/mysql/*; killall -9 mysqld";

    clear_vms();
    m_backends.clear();

    int i = 0;
    while (i < N_MAX)
    {
        string node_name = mxb::string_printf("%s_%03d", prefixc, i);
        if (add_node(nwconfig, node_name))
        {
            auto srv = std::make_unique<mxt::MariaDBServer>(*node(i));
            string key_port = node_name + "_port";
            port[i] = readenv_int(key_port.c_str(), 3306);

            string key_socket = node_name + "_socket";
            string val_socket = envvar_get_set(key_socket.c_str(), "%s", space.c_str());
            m_socket_cmd[i] = (val_socket != space) ? ("--socket=" + val_socket) : space;

            string key_socket_cmd = node_name + "_socket_cmd";
            setenv(key_socket_cmd.c_str(), m_socket_cmd[i].c_str(), 1);

            string key_start_db_cmd = node_name + "_start_db_command";
            m_start_db_command[i] = envvar_get_set(key_start_db_cmd.c_str(), start_db_def);

            string key_stop_db_cmd = node_name + "_stop_db_command";
            m_stop_db_command[i] = envvar_get_set(key_stop_db_cmd.c_str(), stop_db_def);

            string key_clear_db_cmd = node_name + "_cleanup_db_command";
            m_cleanup_db_command[i] = envvar_get_set(key_clear_db_cmd.c_str(), clean_db_def);

            m_backends.push_back(move(srv));
            i++;
        }
        else
        {
            break;
        }
    }


    assert(i == Nodes::n_nodes());
    N = i;
    return i;
}

void MariaDBCluster::print_env()
{
    auto prefixc = prefix().c_str();
    for (int i = 0; i < N; i++)
    {
        printf("%s node %d \t%s\tPort=%d\n", prefixc, i, ip4(i), port[i]);
        printf("%s Access user %s\n", prefixc, access_user(i));
    }
    printf("%s User name %s\n", prefixc, user_name.c_str());
    printf("%s Password %s\n", prefixc, password.c_str());
}

int MariaDBCluster::stop_node(int node)
{
    return ssh_node(node, m_stop_db_command[node], true);
}

int MariaDBCluster::start_node(int node, const char* param)
{
    string cmd = mxb::string_printf("%s %s", m_start_db_command[node].c_str(), param);
    return ssh_node(node, cmd, true);
}

int MariaDBCluster::stop_nodes()
{
    std::vector<std::thread> workers;
    int local_result = 0;
    connect();

    for (int i = 0; i < N; i++)
    {
        workers.emplace_back([&, i]() {
                                 local_result += stop_node(i);
                             });
    }

    for (auto& a : workers)
    {
        a.join();
    }

    return local_result;
}

int MariaDBCluster::stop_slaves()
{
    int i;
    int global_result = 0;
    connect();
    for (i = 0; i < N; i++)
    {
        printf("Stopping slave %d\n", i);
        fflush(stdout);
        global_result += execute_query(nodes[i], (char*) "stop slave;");
    }
    close_connections();
    return global_result;
}

int MariaDBCluster::cleanup_db_node(int node)
{
    return ssh_node(node, m_cleanup_db_command[node], true);
}

int MariaDBCluster::cleanup_db_nodes()
{
    int i;
    int local_result = 0;

    for (i = 0; i < N; i++)
    {
        printf("Cleaning node %d\n", i);
        fflush(stdout);
        local_result += cleanup_db_node(i);
        fflush(stdout);
    }
    return local_result;
}

void MariaDBCluster::create_users(int node)
{
    // Create users for replication as well as the users that are used by the tests
    string str = mxb::string_printf("%s/create_user.sh", m_test_dir.c_str());
    copy_to_node(node, str.c_str(), access_homedir(node));

    ssh_node_f(node, true,
               "export require_ssl=\"%s\"; "
               "export node_user=\"%s\"; "
               "export node_password=\"%s\"; "
               "%s/create_user.sh \"%s\" %s",
               ssl ? "REQUIRE SSL" : "",
               user_name.c_str(),
               password.c_str(),
               access_homedir(0),
               m_socket_cmd[0].c_str(),
               type_string().c_str());
}

int MariaDBCluster::create_users()
{
    for (int i = 0; i < N; i++)
    {
        if (start_node(i, (char*) ""))
        {
            printf("Start of node %d failed\n", i);
            return 1;
        }

        create_users(i);
    }

    return 0;
}

int MariaDBCluster::clean_iptables(int node)
{
    return ssh_node_f(node,
                      true,
                      "while [ \"$(iptables -n -L INPUT 1|grep '%d')\" != \"\" ]; do iptables -D INPUT 1; done;"
                      "while [ \"$(ip6tables -n -L INPUT 1|grep '%d')\" != \"\" ]; do ip6tables -D INPUT 1; done;"
                      "while [ \"$(iptables -n -L OUTPUT 1|grep '3306')\" != \"\" ]; do iptables -D OUTPUT 1; done;",
                      port[node],
                      port[node]);
}


void MariaDBCluster::block_node_from_node(int src, int dest)
{
    std::ostringstream ss;

    ss << "iptables -I OUTPUT 1 -p tcp -d " << ip4(dest) << " --dport 3306 -j DROP;";

    ssh_node_f(src, true, "%s", ss.str().c_str());
}

void MariaDBCluster::unblock_node_from_node(int src, int dest)
{
    std::ostringstream ss;

    ss << "iptables -D OUTPUT -p tcp -d " << ip4(dest) << " --dport 3306 -j DROP;";

    ssh_node_f(src, true, "%s", ss.str().c_str());
}

std::string MariaDBCluster::block_command(int node) const
{
    const char FORMAT[] =
        "iptables -I INPUT -p tcp --dport %d -j REJECT;"
        "ip6tables -I INPUT -p tcp --dport %d -j REJECT";

    char command[sizeof(FORMAT) + 20];

    sprintf(command, FORMAT, port[node], port[node]);

    return command;
}

std::string MariaDBCluster::unblock_command(int node) const
{
    const char FORMAT[] =
        "iptables -I INPUT -p tcp --dport %d -j ACCEPT;"
        "ip6tables -I INPUT -p tcp --dport %d -j ACCEPT";

    char command[sizeof(FORMAT) + 20];

    sprintf(command, FORMAT, port[node], port[node]);

    return command;
}

int MariaDBCluster::block_node(int node)
{
    std::string command = block_command(node);

    int local_result = 0;
    local_result += ssh_node_f(node, true, "%s", command.c_str());

    m_blocked[node] = true;
    return local_result;
}

int MariaDBCluster::unblock_node(int node)
{
    std::string command = unblock_command(node);

    int local_result = 0;
    local_result += clean_iptables(node);
    local_result += ssh_node_f(node, true, "%s", command.c_str());

    m_blocked[node] = false;
    return local_result;
}

int MariaDBCluster::block_all_nodes()
{
    int rval = 0;
    std::vector<std::thread> threads;

    for (int i = 0; i < this->N; i++)
    {
        threads.emplace_back([&, i]() {
                                 rval += this->block_node(i);
                             });
    }

    for (auto& a : threads)
    {
        a.join();
    }

    return rval;
}


int MariaDBCluster::unblock_all_nodes()
{
    int rval = 0;
    std::vector<std::thread> threads;

    for (int i = 0; i < this->N; i++)
    {
        threads.emplace_back([&, i]() {
                                 rval += this->unblock_node(i);
                             });
    }

    for (auto& a : threads)
    {
        a.join();
    }

    return rval;
}

bool MariaDBCluster::fix_replication()
{
    bool rval = true;
    int attempts = 25;

    if (check_replication())
    {
        cout << prefix() << ": Replication is broken, fixing..." << endl;
        rval = false;

        if (unblock_all_nodes() == 0)
        {
            cout << "Prepare nodes" << endl;
            prepare_servers();
            cout << "Starting replication" << endl;
            start_replication();

            while (check_replication() && (attempts > 0))
            {
                cout << "Replication is still broken, waiting" << endl;
                sleep(10);
                attempts--;
            }
            if (check_replication() == 0)
            {
                cout << "Replication is fixed" << endl;
                rval = prepare_for_test();
            }
            else
            {
                cout << "FATAL ERROR: Replication is still broken" << endl;
            }
        }
        else
        {
            cout << "SSH access to nodes doesn't work" << endl;
        }
    }

    return rval;
}

int MariaDBCluster::get_server_id(int index)
{
    int id = -1;
    char str[1024];

    if (find_field(this->nodes[index], "SELECT @@server_id", "@@server_id", (char*) str) == 0)
    {
        id = atoi(str);
    }
    else
    {
        printf("find_field failed for %s:%d\n", ip4(index), this->port[index]);
    }

    return id;
}

std::string MariaDBCluster::get_server_id_str(int index)
{
    std::stringstream ss;
    ss << get_server_id(index);
    return ss.str();
}

std::vector<std::string> MariaDBCluster::get_all_server_ids_str()
{
    std::vector<std::string> rval;

    for (int i = 0; i < N; i++)
    {
        rval.push_back(get_server_id_str(i));
    }

    return rval;
}

std::vector<int> MariaDBCluster::get_all_server_ids()
{
    std::vector<int> rval;

    for (int i = 0; i < N; i++)
    {
        rval.push_back(get_server_id(i));
    }

    return rval;
}

std::string MariaDBCluster::anonymous_users_query() const
{
    return "SELECT CONCAT('\\'', user, '\\'@\\'', host, '\\'') FROM mysql.user WHERE user = ''";
}

bool MariaDBCluster::prepare_for_test(MYSQL* conn)
{
    int local_result = 0;

    if (mysql_query(conn, "FLUSH HOSTS"))
    {
        local_result++;
    }

    if (mysql_query(conn, "SET GLOBAL max_connections=10000"))
    {
        local_result++;
    }

    if (mysql_query(conn, "SET GLOBAL max_connect_errors=10000000"))
    {
        local_result++;
    }

    if (mysql_query(conn, anonymous_users_query().c_str()) == 0)
    {
        MYSQL_RES* res = mysql_store_result(conn);

        if (res)
        {
            std::vector<std::string> users;
            MYSQL_ROW row;

            while ((row = mysql_fetch_row(res)))
            {
                users.push_back(row[0]);
            }

            mysql_free_result(res);

            if (users.size() > 0)
            {
                printf("Detected anonymous users, dropping them.\n");

                for (auto& s : users)
                {
                    std::string query = "DROP USER ";
                    query += s;
                    printf("%s\n", query.c_str());
                    mysql_query(conn, query.c_str());
                }
            }
        }
    }
    else
    {
        printf("Failed to query for anonymous users: %s\n", mysql_error(conn));
        local_result++;
    }

    return local_result == 0;
}

bool MariaDBCluster::prepare_for_test()
{
    if (this->nodes[0] == NULL && (this->connect() != 0))
    {
        return false;
    }

    bool all_ok = true;
    std::vector<std::future<bool>> futures;

    for (int i = 0; i < N; i++)
    {
        bool (MariaDBCluster::* function)(MYSQL*) = &MariaDBCluster::prepare_for_test;
        std::packaged_task<bool(MariaDBCluster*, MYSQL*)> task(function);
        futures.push_back(task.get_future());
        std::thread(std::move(task), this, nodes[i]).detach();
    }

    for (auto& f : futures)
    {
        f.wait();
        if (!f.get())
        {
            all_ok = false;
        }
    }

    return all_ok;
}

int MariaDBCluster::execute_query_all_nodes(const char* sql)
{
    int local_result = 0;
    connect();
    for (int i = 0; i < N; i++)
    {
        local_result += execute_query(nodes[i], "%s", sql);
    }
    close_connections();
    return local_result;
}

int MariaDBCluster::get_version(int i)
{
    int local_result = 0;
    if (find_field(nodes[i], "SELECT @@version", "@@version", version[i]))
    {
        cout << "Failed to get version: " << mysql_error(nodes[i])
             << ", trying ssh node and use MariaDB client" << endl;
        auto res = ssh_output("mysql --batch --silent  -e \"select @@version\"", i, true);
        if (res.rc)
        {
            local_result++;
            cout << "Failed to get version, node " << i << " is broken" << endl;
        }
        else
        {
            strcpy(version[i], res.output.c_str());
        }
    }
    char version_number[256] {0};
    strcpy(version_number, version[i]);
    char* str = strchr(version_number, '-');
    if (str)
    {
        str[0] = 0;
    }
    char version_major[256]{0};
    strcpy(version_major, version_number);
    if (strstr(version_major, "5.") == version_major)
    {
        version_major[3] = 0;
    }
    if (strstr(version_major, "10.") == version_major)
    {
        version_major[4] = 0;
    }

    if (verbose())
    {
        printf("Node %s%d: %s\t %s \t %s\n",
               prefix().c_str(), i, version[i], version_number, version_major);
    }
    return local_result;
}

int MariaDBCluster::get_versions()
{
    int local_result = 0;
    for (int i = 0; i < N; i++)
    {
        local_result += get_version(i);
    }
    return local_result;
}

std::string MariaDBCluster::get_lowest_version()
{
    std::string rval;
    get_versions();

    int lowest = INT_MAX;

    for (int i = 0; i < N; i++)
    {
        int int_version = get_int_version(version[i]);

        if (lowest > int_version)
        {
            rval = version[i];
            lowest = int_version;
        }
    }

    return rval;
}

int MariaDBCluster::truncate_mariadb_logs()
{
    std::vector<std::future<int>> results;

    for (int node = 0; node < N; node++)
    {
        if (strcmp(ip4(node), "127.0.0.1") != 0)
        {
            auto f = std::async(std::launch::async, &Nodes::ssh_node_f, this, node, true,
                                "truncate -s 0 /var/lib/mysql/*.err;"
                                "truncate -s 0 /var/log/syslog;"
                                "truncate -s 0 /var/log/messages;"
                                "rm -f /etc/my.cnf.d/binlog_enc*;");
            results.push_back(std::move(f));
        }
    }

    return std::count_if(results.begin(), results.end(), std::mem_fn(&std::future<int>::get));
}

void MariaDBCluster::close_active_connections()
{
    if (this->nodes[0] == NULL)
    {
        this->connect();
    }

    const char* sql
        =
            "select id from information_schema.processlist where id != @@pseudo_thread_id and user not in ('system user', 'repl')";

    for (int i = 0; i < N; i++)
    {
        if (!mysql_query(nodes[i], sql))
        {
            MYSQL_RES* res = mysql_store_result(nodes[i]);
            if (res)
            {
                MYSQL_ROW row;

                while ((row = mysql_fetch_row(res)))
                {
                    std::string q("KILL ");
                    q += row[0];
                    execute_query_silent(nodes[i], q.c_str());
                }
                mysql_free_result(res);
            }
        }
    }
}


void MariaDBCluster::stash_server_settings(int node)
{
    ssh_node(node, "sudo rm -rf /etc/my.cnf.d.backup/", true);
    ssh_node(node, "sudo mkdir /etc/my.cnf.d.backup/", true);
    ssh_node(node, "sudo cp -r /etc/my.cnf.d/* /etc/my.cnf.d.backup/", true);
}

void MariaDBCluster::restore_server_settings(int node)
{
    ssh_node(node, "sudo mv -f /etc/my.cnf.d.backup/* /etc/my.cnf.d/", true);
}

void MariaDBCluster::disable_server_setting(int node, const char* setting)
{
    ssh_node_f(node, true, "sudo sed -i 's/%s/#%s/' /etc/my.cnf.d/*", setting, setting);
}

void MariaDBCluster::add_server_setting(int node, const char* setting)
{
    ssh_node_f(node, true, "sudo sed -i '$a [server]' /etc/my.cnf.d/*server*.cnf");
    ssh_node_f(node, true, "sudo sed -i '$a %s' /etc/my.cnf.d/*server*.cnf", setting);
}

std::string MariaDBCluster::get_config_name(int node)
{
    std::stringstream ss;
    ss << "server" << node + 1 << ".cnf";
    return ss.str();
}

void MariaDBCluster::reset_server_settings(int node)
{
    std::string cnfdir = m_test_dir + "/mdbci/cnf/";
    std::string cnf = get_config_name(node);

    // Note: This is a CentOS specific path
    ssh_node(node, "rm -rf /etc/my.cnf.d/*", true);
    copy_to_node(node, (cnfdir + cnf).c_str(), "~/");
    ssh_node_f(node, false, "sudo install -o root -g root -m 0644 ~/%s /etc/my.cnf.d/", cnf.c_str());

    // Always configure the backend for SSL
    std::string ssl_dir = m_test_dir + "/ssl-cert";
    std::string ssl_cnf = m_test_dir + "/ssl.cnf";
    copy_to_node_legacy(ssl_dir.c_str(), "~/", node);
    copy_to_node_legacy(ssl_cnf.c_str(), "~/", node);

    ssh_node_f(node, true, "cp %s/ssl.cnf /etc/my.cnf.d/", access_homedir(node));
    ssh_node_f(node, true, "cp -r %s/ssl-cert /etc/", access_homedir(node));
    ssh_node_f(node, true, "chown mysql:mysql -R /etc/ssl-cert");
}

void MariaDBCluster::reset_server_settings()
{
    for (int node = 0; node < N; node++)
    {
        reset_server_settings(node);
    }
}

/**
 * @brief extract_version_from_string Tries to find MariaDB server version number in the output of 'mysqld
 *--version'
 * Function does not allocate any memory
 * @param version String returned by 'mysqld --version'
 * @return pointer to the string with version number
 */
string extract_version_from_string(const string& version)
{
    int pos1 = 0;
    int pos2 = 0;
    int l = version.length();
    while ((!isdigit(version[pos1])) && (pos1 < l))
    {
        pos1++;
    }
    pos2 = pos1;
    while (((isdigit(version[pos2]) || version[pos2] == '.')) && (pos2 < l))
    {
        pos2++;
    }
    return version.substr(pos1, pos2 - pos1);
}

int MariaDBCluster::prepare_server(int i)
{
    cleanup_db_node(i);
    reset_server_settings(i);

    // Note: These should be done by MDBCI
    ssh_node(i, "test -d /etc/apparmor.d/ && "
                "ln -s /etc/apparmor.d/usr.sbin.mysqld /etc/apparmor.d/disable/usr.sbin.mysqld && "
                "sudo service apparmor restart && "
                "chmod a+r -R /etc/my.cnf.d/*", true);

    int rval;
    auto res_version = ssh_output("/usr/sbin/mysqld --version", i, false);
    rval = res_version.rc;

    if (res_version.rc == 0)
    {
        string version_digits = extract_version_from_string(res_version.output);
        auto version_digitsc = version_digits.c_str();

        printf("Detected server version on node %d is %s\n", i, version_digitsc);

        if (memcmp(version_digitsc, "5.", 2) == 0)
        {
            ssh_node(i, "sed -i \"s/binlog_row_image=full//\" /etc/my.cnf.d/*.cnf", true);
        }
        if (memcmp(version_digitsc, "5.7", 3) == 0)
        {
            // Disable 'validate_password' plugin, searach for random temporal
            // password in the log and reseting passord to empty string
            ssh_node(i, "/usr/sbin/mysqld --initialize; sudo chown -R mysql:mysql /var/lib/mysql", true);
            ssh_node(i, m_start_db_command[i], true);
            auto res_temp_pw = ssh_output(
                "cat /var/log/mysqld.log | grep \"temporary password\" | sed -n -e 's/^.*: //p'",
                i, true);
            rval = res_temp_pw.rc;
            auto temp_pw = res_temp_pw.output.c_str();
            ssh_node_f(i, true, "mysqladmin -uroot -p'%s' password '%s'", temp_pw, temp_pw);
            ssh_node_f(i, false,
                       "echo \"UNINSTALL PLUGIN validate_password\" | sudo mysql -uroot -p'%s'", temp_pw);
            ssh_node(i, m_stop_db_command[i], true);
            ssh_node(i, m_start_db_command[i], true);
            ssh_node_f(i, true, "mysqladmin -uroot -p'%s' password ''", temp_pw);
        }
        else
        {
            cout << "Executing mysql_install_db on node" << i << endl;
            ssh_node(i, "mysql_install_db; sudo chown -R mysql:mysql /var/lib/mysql", true);
        }
    }

    stop_node(i);
    start_node(i, "");

    return rval;
}

int MariaDBCluster::prepare_servers()
{
    int rval = 0;
    std::vector<std::thread> threads;

    for (int i = 0; i < N; i++)
    {
        threads.emplace_back([&, i]() {
                                 rval += prepare_server(i);
                             });
    }

    for (auto& a : threads)
    {
        a.join();
    }

    return rval;
}

void MariaDBCluster::limit_nodes(int new_N)
{
    if (N > new_N)
    {
        execute_query_all_nodes((char*) "stop slave;");
        N = new_N;
        fix_replication();
        sleep(10);
    }
}

std::string MariaDBCluster::cnf_servers()
{
    string rval;
    rval.reserve(100 * N);
    bool use_ip6 = using_ipv6();
    for (int i = 0; i < N; i++)
    {
        string one_server = mxb::string_printf("[%s%i]\n"
                                               "type=server\n"
                                               "address=%s\n"
                                               "port=%i\n\n",
                                               m_cnf_server_name.c_str(), i + 1,
                                               use_ip6 ? ip6(i) : ip_private(i),
                                               port[i]);
        rval += one_server;
    }
    return rval;
}

std::string MariaDBCluster::cnf_servers_line()
{
    std::string s = m_cnf_server_name + std::to_string(1);
    for (int i = 1; i < N; i++)
    {
        s += std::string(",") + m_cnf_server_name + std::to_string(i + 1);
    }
    return s;
}

const char* MariaDBCluster::ip(int i) const
{
    return m_use_ipv6 ? Nodes::ip6(i) : Nodes::ip4(i);
}

void MariaDBCluster::set_use_ipv6(bool use_ipv6)
{
    m_use_ipv6 = use_ipv6;
}

const char* MariaDBCluster::ip_private(int i) const
{
    return Nodes::ip_private(i);
}

const char* MariaDBCluster::ip6(int i) const
{
    return Nodes::ip6(i);
}

const char* MariaDBCluster::access_homedir(int i) const
{
    return Nodes::access_homedir(i);
}

const char* MariaDBCluster::access_sudo(int i) const
{
    return Nodes::access_sudo(i);
}

const string& MariaDBCluster::prefix() const
{
    return m_prefix;
}

const char* MariaDBCluster::ip4(int i) const
{
    return Nodes::ip4(i);
}

void MariaDBCluster::disable_ssl()
{
    for (int i = 0; i < N; i++)
    {
        stop_node(i);
        ssh_node(i, "rm -f /etc/my.cnf.d/ssl.cnf", true);
        start_node(i);
    }
}

bool MariaDBCluster::check_ssl(int node)
{
    bool ok = true;
    auto conn = get_connection(node);
    conn.ssl(true);

    if (!conn.connect())
    {
        printf("Failed to connect to database with SSL enabled: %s\n", conn.error());
        ok = false;

        conn.ssl(false);
        printf("Attempting to connect without SSL...\n");

        if (conn.connect())
        {
            printf("Connection was successful, server is not configured with SSL.\n");
        }
        else
        {
            printf("Failed to connect to database with SSL disabled: %s\n", conn.error());
        }
    }
    else
    {
        auto version = conn.field("select variable_value from information_schema.session_status "
                                  "where variable_name like 'ssl_version'");

        if (version.empty())
        {
            printf("Failed to establish SSL connection to database\n");
            ok = false;
        }
        else
        {
            printf("SSL version: %s\n", version.c_str());
        }

        conn.ssl(false);

        if (conn.connect())
        {
            printf("Connection was successful, server does not require SSL.\n");
            ok = false;
        }
    }

    return ok;
}

bool MariaDBCluster::using_ipv6() const
{
    return m_use_ipv6;
}

const std::string& MariaDBCluster::cnf_srv_name() const
{
    return m_cnf_server_name;
}

maxtest::MariaDBServer::MariaDBServer(VMNode& vm)
    : m_vm(vm)
{
}

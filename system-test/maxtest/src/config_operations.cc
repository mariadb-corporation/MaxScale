#include <maxtest/config_operations.hh>
#include <maxtest/replication_cluster.hh>

// The configuration should use these names for the services, listeners and monitors
#define SERVICE_NAME1  "rwsplit-service"
#define SERVICE_NAME2  "read-connection-router-master"
#define SERVICE_NAME3  "read-connection-router-slave"
#define LISTENER_NAME1 "rwsplit-service-listener"
#define LISTENER_NAME2 "read-connection-router-master-listener"
#define LISTENER_NAME3 "read-connection-router-slave-listener"

struct
{
    const char* service;
    const char* listener;
    int         port;
} services[]
{
    {SERVICE_NAME1, LISTENER_NAME1, 4006},
    {SERVICE_NAME2, LISTENER_NAME2, 4008},
    {SERVICE_NAME3, LISTENER_NAME3, 4009}
};

Config::Config(TestConnections* parent)
    : test_(parent)
{
}

Config::~Config()
{
}

void Config::add_server(int num)
{
    test_->tprintf("Adding the servers");
    test_->set_timeout(120);
    test_->maxscales->ssh_node_f(0, true, "maxctrl link service %s server%d", SERVICE_NAME1, num);
    test_->maxscales->ssh_node_f(0, true, "maxctrl link service %s server%d", SERVICE_NAME2, num);
    test_->maxscales->ssh_node_f(0, true, "maxctrl link service %s server%d", SERVICE_NAME3, num);

    for (auto& a : created_monitors_)
    {
        test_->maxscales->ssh_node_f(0, true, "maxctrl link monitor %s server%d", a.c_str(), num);
    }

    test_->stop_timeout();
}

void Config::remove_server(int num)
{
    test_->set_timeout(120);
    test_->maxscales->ssh_node_f(0, true, "maxctrl unlink service %s server%d", SERVICE_NAME1, num);
    test_->maxscales->ssh_node_f(0, true, "maxctrl unlink service %s server%d", SERVICE_NAME2, num);
    test_->maxscales->ssh_node_f(0, true, "maxctrl unlink service %s server%d", SERVICE_NAME3, num);

    for (auto& a : created_monitors_)
    {
        test_->maxscales->ssh_node_f(0, true, "maxctrl unlink monitor %s server%d", a.c_str(), num);
    }

    test_->stop_timeout();
}

void Config::add_created_servers(const char* object)
{
    for (auto a : created_servers_)
    {
        // Not pretty but it should work
        test_->maxscales->ssh_node_f(0, true, "maxctrl link service %s server%d", object, a);
        test_->maxscales->ssh_node_f(0, true, "maxctrl link monitor %s server%d", object, a);
    }
}

void Config::destroy_server(int num)
{
    test_->set_timeout(120);
    test_->maxscales->ssh_node_f(0, true, "maxctrl destroy server server%d", num);
    created_servers_.erase(num);
    test_->stop_timeout();
}

void Config::create_server(int num)
{
    test_->set_timeout(120);
    auto homedir = test_->maxscales->access_homedir(0);
    char ssl_line[200 + 3 * strlen(homedir)] = "";
    if (test_->backend_ssl)
    {
        sprintf(ssl_line,
                " --tls-key=/%s/certs/client-key.pem "
                " --tls-cert=/%s/certs/client-cert.pem "
                " --tls-ca-cert=/%s/certs/ca.pem "
                " --tls-version=MAX "
                " --tls-cert-verify-depth=9",
                homedir, homedir, homedir);
    }
    test_->maxscales->ssh_node_f(0,
                                 true,
                                 "maxctrl create server server%d %s %d %s",
                                 num,
                                 test_->repl->ip_private(num),
                                 test_->repl->port[num],
                                 ssl_line);
    created_servers_.insert(num);
    test_->stop_timeout();
}

void Config::alter_server(int num, const char* key, const char* value)
{
    test_->maxscales->ssh_node_f(0, true, "maxctrl alter server server%d %s %s", num, key, value);
}

void Config::alter_server(int num, const char* key, int value)
{
    test_->maxscales->ssh_node_f(0, true, "maxctrl alter server server%d %s %d", num, key, value);
}

void Config::alter_server(int num, const char* key, float value)
{
    test_->maxscales->ssh_node_f(0, true, "maxctrl alter server server%d %s %f", num, key, value);
}

void Config::create_monitor(const char* name, const char* module, int interval)
{
    test_->set_timeout(120);
    test_->maxscales->ssh_node_f(0, true,
                                 "maxctrl create monitor %s %s monitor_interval=%d user=%s password=%s",
                                 name, module, interval, test_->maxscales->user_name.c_str(),
                                 test_->maxscales->password.c_str());
    test_->stop_timeout();

    created_monitors_.insert(std::string(name));
}

void Config::alter_monitor(const char* name, const char* key, const char* value)
{
    test_->maxscales->ssh_node_f(0, true, "maxctrl alter monitor %s %s %s", name, key, value);
}

void Config::alter_monitor(const char* name, const char* key, int value)
{
    test_->maxscales->ssh_node_f(0, true, "maxctrl alter monitor %s %s %d", name, key, value);
}

void Config::alter_monitor(const char* name, const char* key, float value)
{
    test_->maxscales->ssh_node_f(0, true, "maxctrl alter monitor %s %s %f", name, key, value);
}

void Config::start_monitor(const char* name)
{
    test_->maxscales->ssh_node_f(0, true, "maxctrl start monitor %s", name);
}

void Config::destroy_monitor(const char* name)
{
    test_->set_timeout(120);
    test_->maxscales->ssh_node_f(0, true, "maxctrl destroy monitor %s", name);
    test_->stop_timeout();
    created_monitors_.erase(std::string(name));
}

void Config::restart_monitors()
{
    for (auto& a : created_monitors_)
    {
        test_->maxscales->ssh_node_f(0, true, "maxctrl stop monitor \"%s\"", a.c_str());
        test_->maxscales->ssh_node_f(0, true, "maxctrl start monitor \"%s\"", a.c_str());
    }
}

void Config::create_listener(Config::Service service)
{
    int i = static_cast<int>(service);

    test_->set_timeout(120);
    test_->maxscales->ssh_node_f(0,
                                 true,
                                 "maxctrl create listener %s %s %d",
                                 services[i].service,
                                 services[i].listener,
                                 services[i].port);
    test_->stop_timeout();
}

void Config::create_ssl_listener(Config::Service service)
{
    int i = static_cast<int>(service);

    auto homedir = test_->maxscales->access_homedir(0);
    test_->set_timeout(120);
    test_->maxscales->ssh_node_f(0,
                                 true,
                                 "maxctrl create listener %s %s %d "
                                 "--tls-key=%s/certs/server-key.pem "
                                 "--tls-cert=%s/certs/server-cert.pem "
                                 "--tls-ca-cert=%s/certs/ca.pem ",
                                 services[i].service, services[i].listener, services[i].port,
                                 homedir, homedir, homedir);
    test_->stop_timeout();
}

void Config::destroy_listener(Config::Service service)
{
    int i = static_cast<int>(service);

    test_->set_timeout(120);
    test_->maxscales->ssh_node_f(0,
                                 true,
                                 "maxctrl destroy listener %s %s",
                                 services[i].service,
                                 services[i].listener);
    test_->stop_timeout();
}

void Config::create_all_listeners()
{
    create_listener(SERVICE_RWSPLIT);
    create_listener(SERVICE_RCONN_SLAVE);
    create_listener(SERVICE_RCONN_MASTER);
}

void Config::reset()
{
    /** Make sure the servers exist before checking that connectivity is OK */
    for (int i = 0; i < test_->repl->N; i++)
    {
        if (created_servers_.find(i) == created_servers_.end())
        {
            create_server(i);
            add_server(i);
        }
    }
}

bool Config::check_server_count(int expected)
{
    bool rval = true;

    if (test_->maxscales->ssh_node_f(0,
                                     true,
                                     "test \"`maxctrl list servers --tsv|grep 'server[0-9]'|wc -l`\" == \"%d\"",
                                     expected))
    {
        test_->add_result(1, "Number of servers is not %d.", expected);
        rval = false;
    }

    return rval;
}

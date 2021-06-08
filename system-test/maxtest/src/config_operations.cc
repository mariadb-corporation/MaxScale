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
    test_->maxscale->ssh_node_f(true, "maxctrl link service %s server%d", SERVICE_NAME1, num);
    test_->maxscale->ssh_node_f(true, "maxctrl link service %s server%d", SERVICE_NAME2, num);
    test_->maxscale->ssh_node_f(true, "maxctrl link service %s server%d", SERVICE_NAME3, num);

    for (auto& a : created_monitors_)
    {
        test_->maxscale->ssh_node_f(true, "maxctrl link monitor %s server%d", a.c_str(), num);
    }
}

void Config::remove_server(int num)
{
    test_->maxscale->ssh_node_f(true, "maxctrl unlink service %s server%d", SERVICE_NAME1, num);
    test_->maxscale->ssh_node_f(true, "maxctrl unlink service %s server%d", SERVICE_NAME2, num);
    test_->maxscale->ssh_node_f(true, "maxctrl unlink service %s server%d", SERVICE_NAME3, num);

    for (auto& a : created_monitors_)
    {
        test_->maxscale->ssh_node_f(true, "maxctrl unlink monitor %s server%d", a.c_str(), num);
    }
}

void Config::add_created_servers(const char* object)
{
    for (auto a : created_servers_)
    {
        // Not pretty but it should work
        test_->maxscale->ssh_node_f(true, "maxctrl link service %s server%d", object, a);
        test_->maxscale->ssh_node_f(true, "maxctrl link monitor %s server%d", object, a);
    }
}

void Config::destroy_server(int num)
{
    test_->maxscale->ssh_node_f(true, "maxctrl destroy server server%d", num);
    created_servers_.erase(num);
}

void Config::create_server(int num)
{
    auto homedir = test_->maxscale->access_homedir();
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
    test_->maxscale->ssh_node_f(true,
                                "maxctrl create server server%d %s %d %s",
                                num,
                                test_->repl->ip_private(num),
                                test_->repl->port[num],
                                ssl_line);
    created_servers_.insert(num);
}

void Config::alter_server(int num, const char* key, const char* value)
{
    test_->maxscale->ssh_node_f(true, "maxctrl alter server server%d %s %s", num, key, value);
}

void Config::alter_server(int num, const char* key, int value)
{
    test_->maxscale->ssh_node_f(true, "maxctrl alter server server%d %s %d", num, key, value);
}

void Config::alter_server(int num, const char* key, float value)
{
    test_->maxscale->ssh_node_f(true, "maxctrl alter server server%d %s %f", num, key, value);
}

void Config::create_monitor(const char* name, const char* module, int interval)
{
    test_->maxscale->ssh_node_f(true,
                                "maxctrl create monitor %s %s monitor_interval=%d user=%s password=%s",
                                name, module, interval, test_->maxscale->user_name.c_str(),
                                test_->maxscale->password.c_str());
    created_monitors_.insert(std::string(name));
}

void Config::alter_monitor(const char* name, const char* key, const char* value)
{
    test_->maxscale->ssh_node_f(true, "maxctrl alter monitor %s %s %s", name, key, value);
}

void Config::alter_monitor(const char* name, const char* key, int value)
{
    test_->maxscale->ssh_node_f(true, "maxctrl alter monitor %s %s %d", name, key, value);
}

void Config::alter_monitor(const char* name, const char* key, float value)
{
    test_->maxscale->ssh_node_f(true, "maxctrl alter monitor %s %s %f", name, key, value);
}

void Config::start_monitor(const char* name)
{
    test_->maxscale->ssh_node_f(true, "maxctrl start monitor %s", name);
}

void Config::destroy_monitor(const char* name)
{
    test_->maxscale->ssh_node_f(true, "maxctrl destroy monitor %s", name);
    created_monitors_.erase(std::string(name));
}

void Config::restart_monitors()
{
    for (auto& a : created_monitors_)
    {
        test_->maxscale->ssh_node_f(true, "maxctrl stop monitor \"%s\"", a.c_str());
        test_->maxscale->ssh_node_f(true, "maxctrl start monitor \"%s\"", a.c_str());
    }
}

void Config::create_listener(Config::Service service)
{
    int i = static_cast<int>(service);

    test_->maxscale->ssh_node_f(true,
                                "maxctrl create listener %s %s %d",
                                services[i].service,
                                services[i].listener,
                                services[i].port);
}

void Config::create_ssl_listener(Config::Service service)
{
    int i = static_cast<int>(service);

    auto homedir = test_->maxscale->access_homedir();
    test_->maxscale->ssh_node_f(true,
                                "maxctrl create listener %s %s %d "
                                "--tls-key=%s/certs/server-key.pem "
                                "--tls-cert=%s/certs/server-cert.pem "
                                "--tls-ca-cert=%s/certs/ca.pem ",
                                services[i].service, services[i].listener, services[i].port,
                                homedir, homedir, homedir);
}

void Config::destroy_listener(Config::Service service)
{
    int i = static_cast<int>(service);

    test_->maxscale->ssh_node_f(true,
                                "maxctrl destroy listener %s %s",
                                services[i].service,
                                services[i].listener);
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

    if (test_->maxscale->ssh_node_f(true,
                                    "test \"`maxctrl list servers --tsv|grep 'server[0-9]'|wc -l`\" == \"%d\"",
                                    expected))
    {
        test_->add_result(1, "Number of servers is not %d.", expected);
        rval = false;
    }

    return rval;
}

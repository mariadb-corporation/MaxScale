#include "config_operations.h"

// The configuration should use these names for the services, listeners and monitors
#define SERVICE_NAME1 "rwsplit-service"
#define SERVICE_NAME2 "read-connection-router-master"
#define SERVICE_NAME3 "read-connection-router-slave"
#define LISTENER_NAME1 "rwsplit-service-listener"
#define LISTENER_NAME2 "read-connection-router-master-listener"
#define LISTENER_NAME3 "read-connection-router-slave-listener"

struct
{
    const char *service;
    const char *listener;
    int         port;
} services[]
{
    {SERVICE_NAME1, LISTENER_NAME1, 4006},
    {SERVICE_NAME2, LISTENER_NAME2, 4008},
    {SERVICE_NAME3, LISTENER_NAME3, 4009}
};

Config::Config(TestConnections* parent):
    test_(parent)
{
}

Config::~Config()
{
}

void Config::add_server(int num)
{
    test_->tprintf("Adding the servers");
    test_->set_timeout(120);
    test_->ssh_maxscale(true, "maxadmin add server server%d " SERVICE_NAME1, num);
    test_->ssh_maxscale(true, "maxadmin add server server%d " SERVICE_NAME2, num);
    test_->ssh_maxscale(true, "maxadmin add server server%d " SERVICE_NAME3, num);

    for (auto& a : created_monitors_)
    {
        test_->ssh_maxscale(true, "maxadmin add server server%d %s", num, a.c_str());
    }

    test_->stop_timeout();
}

void Config::remove_server(int num)
{
    test_->set_timeout(120);
    test_->ssh_maxscale(true, "maxadmin remove server server%d " SERVICE_NAME1, num);
    test_->ssh_maxscale(true, "maxadmin remove server server%d " SERVICE_NAME2, num);
    test_->ssh_maxscale(true, "maxadmin remove server server%d " SERVICE_NAME3, num);

    for (auto& a : created_monitors_)
    {
        test_->ssh_maxscale(true, "maxadmin remove server server%d %s", num, a.c_str());
    }

    test_->stop_timeout();
}

void Config::add_created_servers(const char *object)
{
    for (auto a : created_servers_)
    {
        test_->ssh_maxscale(true, "maxadmin add server server%d %s", a, object);
    }
}

void Config::destroy_server(int num)
{
    test_->set_timeout(120);
    test_->ssh_maxscale(true, "maxadmin destroy server server%d", num);
    created_servers_.erase(num);
    test_->stop_timeout();
}

void Config::create_server(int num)
{
    test_->set_timeout(120);
    test_->ssh_maxscale(true, "maxadmin create server server%d %s %d",
                        num, test_->repl->IP[num], test_->repl->port[num]);
    created_servers_.insert(num);
    test_->stop_timeout();
}

void Config::alter_server(int num, const char *key, const char *value)
{
    test_->ssh_maxscale(true, "maxadmin alter server server%d %s=%s", num, key, value);
}

void Config::alter_server(int num, const char *key, int value)
{
    test_->ssh_maxscale(true, "maxadmin alter server server%d %s=%d", num, key, value);
}

void Config::alter_server(int num, const char *key, float value)
{
    test_->ssh_maxscale(true, "maxadmin alter server server%d %s=%f", num, key, value);
}

void Config::create_monitor(const char *name, const char *module, int interval)
{
    test_->set_timeout(120);
    test_->ssh_maxscale(true, "maxadmin create monitor %s %s", name, module);
    alter_monitor(name, "monitor_interval", interval);
    alter_monitor(name, "user", test_->maxscale_user);
    alter_monitor(name, "password", test_->maxscale_password);
    test_->ssh_maxscale(true, "maxadmin restart monitor %s", name);
    test_->stop_timeout();

    created_monitors_.insert(std::string(name));
}

void Config::alter_monitor(const char* name, const char *key, const char *value)
{
    test_->ssh_maxscale(true, "maxadmin alter monitor %s %s=%s", name, key, value);
}

void Config::alter_monitor(const char* name, const char *key, int value)
{
    test_->ssh_maxscale(true, "maxadmin alter monitor %s %s=%d", name, key, value);
}

void Config::alter_monitor(const char* name, const char *key, float value)
{
    test_->ssh_maxscale(true, "maxadmin alter monitor %s %s=%f", name, key, value);
}

void Config::start_monitor(const char *name)
{
    test_->ssh_maxscale(true, "maxadmin restart monitor %s", name);
}

void Config::destroy_monitor(const char *name)
{
    test_->set_timeout(120);
    test_->ssh_maxscale(true, "maxadmin destroy monitor %s", name);
    test_->stop_timeout();
    created_monitors_.erase(std::string(name));
}

void Config::restart_monitors()
{
    for (auto& a : created_monitors_)
    {
        test_->ssh_maxscale(true, "maxadmin shutdown monitor \"%s\"", a.c_str());
        test_->ssh_maxscale(true, "maxadmin restart monitor \"%s\"", a.c_str());
    }
}

void Config::create_listener(Config::Service service)
{
    int i = static_cast<int>(service);

    test_->set_timeout(120);
    test_->ssh_maxscale(true, "maxadmin create listener %s %s default %d",
                        services[i].service,
                        services[i].listener,
                        services[i].port);
    test_->stop_timeout();
}

void Config::create_ssl_listener(Config::Service service)
{
    int i = static_cast<int>(service);

    test_->set_timeout(120);
    test_->ssh_maxscale(true, "maxadmin create listener %s %s default %d default default default "
                        "/home/vagrant/certs/server-key.pem "
                        "/home/vagrant/certs/server-cert.pem "
                        "/home/vagrant/certs/ca.pem ",
                        services[i].service,
                        services[i].listener,
                        services[i].port);
    test_->stop_timeout();
}

void Config::destroy_listener(Config::Service service)
{
    int i = static_cast<int>(service);

    test_->set_timeout(120);
    test_->ssh_maxscale(true, "maxadmin destroy listener %s %s",
                        services[i].service, services[i].listener);
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

    if (test_->ssh_maxscale(true, "test \"`maxadmin list servers|grep 'server[0-9]'|wc -l`\" == \"%d\"",
                            expected))
    {
        test_->add_result(1, "Number of servers is not %d.", expected);
        rval = false;
    }

    return rval;
}

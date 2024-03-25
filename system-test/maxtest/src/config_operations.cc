/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/format.hh>
#include <maxtest/config_operations.hh>
#include <maxtest/replication_cluster.hh>

namespace
{
// The configuration should use these names for the services, listeners and monitors
const char SERVICE_NAME1[] = "rwsplit-service";
const char SERVICE_NAME2[] = "read-connection-router-master";
const char SERVICE_NAME3[] = "read-connection-router-slave";
const char LISTENER_NAME1[] = "rwsplit-service-listener";
const char LISTENER_NAME2[] = "read-connection-router-master-listener";
const char LISTENER_NAME3[] = "read-connection-router-slave-listener";

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
}

Config::Config(TestConnections* parent)
    : test_(parent)
    , mxs(test_->maxscale)
{
}

Config::~Config()
{
}

void Config::add_server(int num)
{
    test_->tprintf("Adding server %i", num);
    const char link[] = "link service %s server%d";
    mxs->maxctrlf(link, SERVICE_NAME1, num);
    mxs->maxctrlf(link, SERVICE_NAME2, num);
    mxs->maxctrlf(link, SERVICE_NAME3, num);

    for (auto& a : created_monitors_)
    {
        mxs->maxctrlf("link monitor %s server%d", a.c_str(), num);
    }
}

void Config::remove_server(int num)
{
    test_->tprintf("Removing server %i", num);
    const char remove[] = "unlink service %s server%d";
    mxs->maxctrlf(remove, SERVICE_NAME1, num);
    mxs->maxctrlf(remove, SERVICE_NAME2, num);
    mxs->maxctrlf(remove, SERVICE_NAME3, num);

    for (auto& a : created_monitors_)
    {
        mxs->maxctrlf("unlink monitor %s server%d", a.c_str(), num);
    }
}

void Config::add_created_servers(const char* object)
{
    for (auto a : created_servers_)
    {
        // Not pretty but it should work
        auto res1 = mxs->maxctrl(mxb::string_printf("link service %s server%d", object, a));
        auto res2 = mxs->maxctrl(mxb::string_printf("link monitor %s server%d", object, a));
        test_->expect((res1.rc != 0) != (res2.rc != 0),
                      "Expected one link command to succeed and the other to fail.");
    }
}

void Config::destroy_server(int num, Expect expect)
{
    auto cmd = mxb::string_printf("destroy server server%d", num);
    auto res = mxs->maxctrl(cmd);
    check_result(res, cmd, expect);
    if (res.rc == 0)
    {
        created_servers_.erase(num);
    }
}

void Config::create_server(int num, Expect expect)
{
    char ssl_line[1024];
    ssl_line[0] = '\0';

    if (test_->backend_ssl)
    {
        auto key = mxs->cert_key_path();
        auto cert = mxs->cert_path();
        auto ca_cert = mxs->ca_cert_path();
        sprintf(ssl_line,
                " ssl=true"
                " ssl_key=%s ssl_cert=%s ssl_ca=%s "
                " ssl_version=MAX "
                " ssl_cert_verify_depth=9",
                key.c_str(), cert.c_str(), ca_cert.c_str());
    }
    auto* srv = test_->repl->backend(num);
    auto cmd = mxb::string_printf("create server server%d %s %d %s",
                                  num, srv->ip_private(), srv->port(), ssl_line);
    auto res = mxs->maxctrl(cmd);
    check_result(res, cmd, expect);
    if (res.rc == 0)
    {
        created_servers_.insert(num);
    }
}

void Config::alter_server(int num, const char* key, const char* value)
{
    mxs->maxctrlf("alter server server%d %s %s", num, key, value);
}

void Config::alter_server(int num, const char* key, int value)
{
    mxs->maxctrlf("alter server server%d %s %d", num, key, value);
}

void Config::alter_server(int num, const char* key, float value)
{
    mxs->maxctrlf("alter server server%d %s %f", num, key, value);
}

void Config::create_monitor(const char* name, const char* module, int interval)
{
    mxs->maxctrlf("create monitor %s %s monitor_interval=%dms user=%s password=%s",
                  name, module, interval, mxs->user_name().c_str(),
                  mxs->password().c_str());
    created_monitors_.insert(std::string(name));
}

void Config::alter_monitor(const char* name, const char* key, const char* value)
{
    mxs->maxctrlf("alter monitor %s %s %s", name, key, value);
}

void Config::alter_monitor(const char* name, const char* key, int value)
{
    mxs->maxctrlf("alter monitor %s %s %d", name, key, value);
}

void Config::alter_monitor(const char* name, const char* key, float value)
{
    mxs->maxctrlf("alter monitor %s %s %f", name, key, value);
}

void Config::start_monitor(const char* name)
{
    mxs->maxctrlf("start monitor %s", name);
}

void Config::destroy_monitor(const char* name)
{
    mxs->maxctrlf("destroy monitor %s", name);
    created_monitors_.erase(std::string(name));
}

void Config::restart_monitors()
{
    for (auto& a : created_monitors_)
    {
        mxs->maxctrlf("stop monitor \"%s\"", a.c_str());
        mxs->maxctrlf("start monitor \"%s\"", a.c_str());
    }
}

void Config::create_listener(Config::Service service, Expect expect)
{
    int i = static_cast<int>(service);
    auto cmd = mxb::string_printf("create listener %s %s %d",
                                  services[i].service, services[i].listener, services[i].port);
    auto res = mxs->maxctrl(cmd);
    check_result(res, cmd, expect);
}

void Config::create_ssl_listener(Config::Service service)
{
    int i = static_cast<int>(service);
    auto key = mxs->cert_key_path();
    auto cert = mxs->cert_path();
    auto ca_cert = mxs->ca_cert_path();

    mxs->maxctrlf("create listener %s %s %d "
                  "ssl=true "
                  "ssl_key=%s ssl_cert=%s ssl_ca=%s ",
                  services[i].service, services[i].listener, services[i].port,
                  key.c_str(), cert.c_str(), ca_cert.c_str());
}

void Config::destroy_listener(Config::Service service)
{
    int i = static_cast<int>(service);
    mxs->maxctrlf("destroy listener %s %s", services[i].service, services[i].listener);
}

void Config::create_all_listeners(Expect expect)
{
    create_listener(SERVICE_RWSPLIT, expect);
    create_listener(SERVICE_RCONN_SLAVE, expect);
    create_listener(SERVICE_RCONN_MASTER, expect);
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

void Config::check_server_count(int expected)
{
    auto servers = mxs->get_servers();
    test_->expect((int)servers.size() == expected, "Found %zu servers when %i was expected.",
                  servers.size(), expected);
}

void Config::check_result(const mxt::CmdResult& res, const std::string& cmd, Expect expect)
{
    bool success = res.rc == 0;
    bool expected = (expect == Expect::SUCCESS);
    if (success != expected)
    {
        if (success)
        {
            test_->add_failure("MaxCtrl command '%s' succeeded when failure was expected.",
                               cmd.c_str());
        }
        else
        {
            test_->add_failure("MaxCtrl command '%s' failed when success was expected. Error %i : %s",
                               cmd.c_str(), res.rc, res.output.c_str());
        }
    }
}

/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#pragma once

#include <maxtest/testconnections.hh>
#include <string>
#include <set>
#include <sstream>

namespace maxtest
{
class Config
{
public:
    Config(TestConnections* parent);
    ~Config();

    /**
     * Service identifiers for listener creation
     */
    enum Service
    {
        SERVICE_RWSPLIT      = 0,
        SERVICE_RCONN_SLAVE  = 1,
        SERVICE_RCONN_MASTER = 2
    };

    using Expect = mxt::MaxScale::Expect;

    /**
     * Add a server to all services and monitors
     *
     * @param num Backend number
     */
    void add_server(int num, Expect expect = Expect::SUCCESS);

    /**
     * Add all created servers to an object
     *
     * @param object Object to add servers to
     */
    void add_created_servers(const char* object);

    /**
     * Remove a server
     *
     * @param num Backend number
     */
    void remove_server(int num);

    /**
     * Create a new server
     *
     * @param num Backend number
     * @param expect Should the operation succeed?
     */
    void create_server(int num, Expect expect = Expect::SUCCESS);

    /**
     * Alter a server
     *
     * @param num Backend number
     * @param key Key to alter
     * @oaram value Value for @c key, empty string for no value
     */
    void alter_server(int num, const char* key, const char* value);
    void alter_server(int num, const char* key, int value);
    void alter_server(int num, const char* key, float value);

    template<class K, class V, class ...Args>
    void alter_server(int num, K k, V v, Args... args)
    {
        test_->maxscale->maxctrlf("alter server server%d %s", num,
                                  create_alter_server_params(k, v, args...).c_str());
    }

    /**
     * Destroy a server
     * @param num Backend number
     * @param expect Should the operation succeed?
     */
    void destroy_server(int num, Expect expect = Expect::SUCCESS);

    /**
     * Test that server count is at the expected amount
     * @param expected How many servers are expected to exist
     */
    void check_server_count(int expected);

    /**
     * Create the monitor
     * @param type The name of the monitor module to use
     * @param interval Monitoring interval
     */
    void create_monitor(const char* name, const char* module, int interval = 1000);

    /**
     * Start the created monitor
     */
    void start_monitor(const char* name);

    /**
     * Alter a monitor
     * @param key Key to alter
     * @oaram value Value for @c key, empty string for no value
     */
    void alter_monitor(const char* name, const char* key, const char* value);
    void alter_monitor(const char* name, const char* key, int value);
    void alter_monitor(const char* name, const char* key, float value);

    /**
     * Destroy the monitor
     */
    void destroy_monitor(const char* name);

    /**
     * Restart all created monitors
     */
    void restart_monitors();

    /**
     * Create a listener
     *
     * @param service Service where listener is created
     * @param expect Should the operation succeed?
     */
    void create_listener(Service service, Expect expect = Expect::SUCCESS);


    /**
     * Create a listener with SSL enabled
     *
     * @param service Service where SSL listener is created
     */
    void create_ssl_listener(Service service);

    /**
     * Destroy a listener
     *
     * @param service Service whose listener is destroyed
     */
    void destroy_listener(Service service);

    /**
     * Create all basic listeners
     *
     * @param expect Should the operation succeed?
     */
    void create_all_listeners(Expect expect = Expect::SUCCESS);

    /**
     * Reset the configuration to a standard state
     */
    void reset();

private:
    TestConnections*      test_;
    std::set<int>         created_servers_;
    std::set<std::string> created_monitors_;
    mxt::MaxScale*        mxs;

    template<class K, class V, class ...Args>
    std::string create_alter_server_params(K k, V v, Args... args)
    {
        std::ostringstream ss;
        ss << k << " " << v << " ";
        ss << create_alter_server_params(args...);
        return ss.str();
    }

    std::string create_alter_server_params()
    {
        return "";
    }
};
}

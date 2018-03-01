#pragma once

#include "testconnections.h"
#include <string>
#include <set>

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
        SERVICE_RWSPLIT = 0,
        SERVICE_RCONN_SLAVE = 1,
        SERVICE_RCONN_MASTER = 2
    };

    /**
     * Add a server to all services and monitors
     *
     * @param num Backend number
     */
    void add_server(int num);

    /**
     * Add all created servers to an object
     *
     * @param object Object to add servers to
     */
    void add_created_servers(const char *object);

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
     */
    void create_server(int num);

    /**
     * Alter a server
     *
     * @param num Backend number
     * @param key Key to alter
     * @oaram value Value for @c key, empty string for no value
     */
    void alter_server(int num, const char *key, const char *value);
    void alter_server(int num, const char *key, int value);
    void alter_server(int num, const char *key, float value);

    /**
     * Destroy a server
     * @param num Backend number
     */
    void destroy_server(int num);

    /**
     * Test that server count is at the expected amount
     * @param expected How many servers are expected to exist
     * @return True if the number of servers is @c expected
     */
    bool check_server_count(int expected);

    /**
     * Create the monitor
     * @param type The name of the monitor module to use
     * @param interval Monitoring interval
     */
    void create_monitor(const char *name, const char *module, int interval = 1000);

    /**
     * Start the created monitor
     */
    void start_monitor(const char *name);

    /**
     * Alter a monitor
     * @param key Key to alter
     * @oaram value Value for @c key, empty string for no value
     */
    void alter_monitor(const char *name, const char *key, const char *value);
    void alter_monitor(const char *name, const char *key, int value);
    void alter_monitor(const char *name, const char *key, float value);

    /**
     * Destroy the monitor
     */
    void destroy_monitor(const char *name);

    /**
     * Restart all created monitors
     */
    void restart_monitors();

    /**
     * Create a listener
     *
     * @param service Service where listener is created
     */
    void create_listener(Service service);


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
     */
    void create_all_listeners();

    /**
     * Reset the configuration to a standard state
     */
    void reset();

private:
    TestConnections *test_;
    std::set<int> created_servers_;
    std::set<std::string> created_monitors_;
};

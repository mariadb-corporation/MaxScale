/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * Internal header for the server type
 */

#include <maxbase/ccdefs.hh>

#include <map>
#include <mutex>
#include <maxbase/average.hh>
#include <maxscale/config.hh>
#include <maxscale/server.hh>
#include <maxscale/resultset.hh>

std::unique_ptr<ResultSet> serverGetList();

// Private server implementation
class Server : public SERVER
{
public:
    Server(const std::string& name, const std::string& protocol = "", const std::string& authenticator = "")
        : SERVER()
        , m_name(name)
        , m_response_time(maxbase::EMAverage {0.04, 0.35, 500})
    {
        m_settings.protocol = protocol;
        m_settings.authenticator = authenticator;
    }

    struct ConfigParameter
    {
        std::string name;
        std::string value;
    };

    int response_time_num_samples() const
    {
        return m_response_time.num_samples();
    }

    double response_time_average() const
    {
        return m_response_time.average();
    }

    void response_time_add(double ave, int num_samples);

    long persistpoolmax() const
    {
        return m_settings.persistpoolmax;
    }

    void set_persistpoolmax(long persistpoolmax)
    {
        m_settings.persistpoolmax = persistpoolmax;
    }

    long persistmaxtime() const
    {
        return m_settings.persistmaxtime;
    }

    void set_persistmaxtime(long persistmaxtime)
    {
        m_settings.persistmaxtime = persistmaxtime;
    }

    bool have_disk_space_limits() const override
    {
        std::lock_guard<std::mutex> guard(m_settings.lock);
        return !m_settings.disk_space_limits.empty();
    }

    MxsDiskSpaceThreshold get_disk_space_limits() const override
    {
        std::lock_guard<std::mutex> guard(m_settings.lock);
        return m_settings.disk_space_limits;
    }

    void set_disk_space_limits(const MxsDiskSpaceThreshold& new_limits) override
    {
        std::lock_guard<std::mutex> guard(m_settings.lock);
        m_settings.disk_space_limits = new_limits;
    }

    bool persistent_conns_enabled() const override
    {
        return m_settings.persistpoolmax > 0;
    }

    void set_version(uint64_t version_num, const std::string& version_str) override;

    Version version() const override
    {
        return info.version_num();
    }

    Type type() const override
    {
        return info.type();
    }

    std::string version_string() const override
    {
        return info.version_string();
    }

    const char* name() const override
    {
        return m_name.c_str();
    }

    std::string protocol() const override
    {
        return m_settings.protocol;
    }

    std::string get_authenticator() const
    {
        return m_settings.authenticator;
    }

    /**
     * Get a DCB from the persistent connection pool, if possible
     *
     * @param user        The name of the user needing the connection
     * @param ip          Client IP address
     * @param protocol    The name of the protocol needed for the connection
     * @param id          Thread ID
     *
     * @return A DCB or NULL if no connection is found
     */
    DCB* get_persistent_dcb(const std::string& user, const std::string& ip, const std::string& protocol,
                            int id);

    /**
     * Print server details to a dcb.
     *
     * @param dcb Dcb to print to
     */
    void print_to_dcb(DCB* dcb) const;

    /**
     * @brief Allocate a new server
     *
     * This will create a new server that represents a backend server that services
     * can use. This function will add the server to the running configuration but
     * will not persist the changes.
     *
     * @param name   Unique server name
     * @param params Parameters for the server
     *
     * @return       The newly created server or NULL if an error occurred
     */
    static Server* server_alloc(const char* name, MXS_CONFIG_PARAMETER* params);

    /**
     * Creates a server without any configuration. This should be used in unit tests in place of
     * a default ctor.
     *
     * @return A new server
     */
    static Server* create_test_server();

    /**
     * @brief Find a server with the specified name
     *
     * @param name Name of the server
     * @return The server or NULL if not found
     */
    static Server* find_by_unique_name(const std::string& name);

    /**
     * Test if name is a normal server setting name.
     *
     * @param name Name to check
     * @return True if name is a standard parameter
     */
    bool is_custom_parameter(const std::string& name) const;

    /**
     * Print server details to a DCB
     *
     * Designed to be called within a debugger session in order
     * to display all active servers within the gateway
     */
    static void dprintServer(DCB*, const Server*);

    /**
     * Diagnostic to print number of DCBs in persistent pool for a server
     *
     * @param       pdcb    DCB to print results to
     * @param       server  SERVER for which DCBs are to be printed
     */
    static void dprintPersistentDCBs(DCB*, const Server*);

    /**
     * Print all servers to a DCB
     *
     * Designed to be called within a debugger session in order
     * to display all active servers within the gateway
     */
    static void dprintAllServers(DCB*);

    /**
     * Print all servers in Json format to a DCB
     */
    static void dprintAllServersJson(DCB*);

    /**
     * List all servers in a tabular form to a DCB
     *
     */
    static void dListServers(DCB*);

    static bool create_server_config(const Server* server, const char* filename);

    static json_t* server_json_attributes(const Server* server);

    /**
     * @brief Set server parameter
     *
     * @param server Server to update
     * @param name   Parameter to set
     * @param value  Value of parameter
     */
    void set_parameter(const std::string& name, const std::string& value);

    std::string get_custom_parameter(const std::string& name) const override;

    /**
     * @brief Serialize a server to a file
     *
     * This converts @c server into an INI format file. This allows created servers
     * to be persisted to disk. This will replace any existing files with the same
     * name.
     *
     * @return False if the serialization of the server fails, true if it was successful
     */
    bool serialize() const;

    mutable std::mutex m_lock;
    DCB**              persistent = nullptr; /**< List of unused persistent connections to the server */

private:
    struct Settings
    {
        mutable std::mutex lock; /**< Protects array-like settings from concurrent access */

        std::string protocol;      /**< Backend protocol module name */
        std::string authenticator; /**< Authenticator module name */

        /** Disk space thresholds. Can be queried from modules at any time so access must be protected
         *  by mutex. */
        MxsDiskSpaceThreshold disk_space_limits;
        /** All config settings in text form. This is only read and written from the admin thread
         *  so no need for locking. */
        std::vector<ConfigParameter> all_parameters;
        /** Additional custom parameters which may affect routing decisions or the monitor module.
         *  Can be queried from modules at any time so access must be protected by mutex. */
        std::map<std::string, std::string> custom_parameters;

        long persistpoolmax = 0; /**< Maximum size of persistent connections pool */
        long persistmaxtime = 0; /**< Maximum number of seconds connection can live */
    };

    /**
     * Stores server version info. Encodes/decodes to/from the version number received from the server.
     * Also stores the version string and parses information from it. */
    class VersionInfo
    {
    public:

        /**
         * Reads in version data. Deduces server type from version string.
         *
         * @param version_num Version number from server
         * @param version_string Version string from server
         */
        void set(uint64_t version_num, const std::string& version_string);

        Version version_num() const;
        Type type() const;
        std::string version_string() const;

    private:
        mutable std::mutex m_lock;                    /**< Protects against concurrent writing */
        Version m_version_num;                        /**< Numeric version */
        Type m_type = Type::MARIADB;                  /**< Server type */
        char m_version_str[MAX_VERSION_LEN] = {'\0'}; /**< Server version string */
    };

    const std::string m_name;             /**< Server config name */
    Settings m_settings;                  /**< Server settings */
    VersionInfo info;                     /**< Server version and type information */
    maxbase::EMAverage m_response_time;   /**< Response time calculations for this server */
};

void server_free(Server* server);

/**
 * @brief Convert a server to JSON format
 *
 * @param server Server to convert
 * @param host    Hostname of this server
 *
 * @return JSON representation of server or NULL if an error occurred
 */
json_t* server_to_json(const Server* server, const char* host);

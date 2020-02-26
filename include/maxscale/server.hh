/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-02-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <mutex>
#include <string>
#include <unordered_map>
#include <maxbase/average.hh>
#include <maxscale/ssl.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/target.hh>
#include <maxscale/workerlocal.hh>

/**
 * Server configuration parameters names
 */
extern const char CN_MONITORPW[];
extern const char CN_MONITORUSER[];
extern const char CN_PERSISTMAXTIME[];
extern const char CN_PERSISTPOOLMAX[];
extern const char CN_PROXY_PROTOCOL[];

/**
 * The SERVER structure defines a backend server. Each server has a name
 * or IP address for the server, a port that the server listens on and
 * the name of a protocol module that is loaded to implement the protocol
 * between the gateway and the server.
 */
class SERVER : public mxs::Target
{
public:
    static const int MAX_ADDRESS_LEN = 1024;
    static const int MAX_MONUSER_LEN = 512;
    static const int MAX_MONPW_LEN = 512;
    static const int MAX_VERSION_LEN = 256;

    // A mapping from a path to a percentage, e.g.: "/disk" -> 80.
    typedef std::unordered_map<std::string, int32_t> DiskSpaceLimits;

    enum class Type
    {
        MARIADB,
        MYSQL,
        CLUSTRIX
    };

    struct Version
    {
        uint64_t total = 0;     /**< The version number received from server */
        uint32_t major = 0;     /**< Major version */
        uint32_t minor = 0;     /**< Minor version */
        uint32_t patch = 0;     /**< Patch version */
    };

    struct PoolStats
    {
        int      n_persistent = 0;  /**< Current persistent pool */
        uint64_t n_new_conn = 0;    /**< Times the current pool was empty */
        uint64_t n_from_pool = 0;   /**< Times when a connection was available from the pool */
    };

    // Base settings
    char address[MAX_ADDRESS_LEN + 1] = {'\0'}; /**< Server hostname/IP-address */
    int  port = -1;                             /**< Server port */
    int  extra_port = -1;                       /**< Alternative monitor port if normal port fails */

    // Other settings
    bool proxy_protocol = false;    /**< Send proxy-protocol header to backends when connecting
                                     * routing sessions. */

    // Base variables
    bool    is_active = false;          /**< Server is active and has not been "destroyed" */
    uint8_t charset = DEFAULT_CHARSET;  /**< Character set. Read from backend and sent to client. */

    // Statistics and events
    PoolStats pool_stats;
    int       persistmax = 0;       /**< Maximum pool size actually achieved since startup */

    int           rlag = mxs::Target::RLAG_UNDEFINED;   /**< Replication Lag for Master/Slave replication */
    unsigned long node_ts = 0;                          /**< Last timestamp set from M/S monitor module */

    // Misc fields
    bool master_err_is_logged = false;  /**< If node failed, this indicates whether it is logged. Only
                                         * used by rwsplit. TODO: Move to rwsplit */
    bool warn_ssl_not_enabled = true;   /**< SSL not used for an SSL enabled server */

    virtual ~SERVER() = default;

    /**
     * Check if server has disk space threshold settings.
     *
     * @return True if limits exist
     */
    virtual bool have_disk_space_limits() const = 0;

    /**
     * Get a copy of disk space limit settings.
     *
     * @return A copy of settings
     */
    virtual DiskSpaceLimits get_disk_space_limits() const = 0;

    /**
     * Is persistent connection pool enabled.
     *
     * @return True if enabled
     */
    virtual bool persistent_conns_enabled() const = 0;

    /**
     * Fetch value of custom parameter.
     *
     * @param name Parameter name
     * @return Value of parameter, or empty if not found
     */
    virtual std::string get_custom_parameter(const std::string& name) const = 0;

    /**
     * Update server version.
     *
     * @param version_num New numeric version
     * @param version_str New version string
     */
    virtual void set_version(uint64_t version_num, const std::string& version_str) = 0;

    /**
     * Get numeric version information.
     *
     * @return Major, minor and patch numbers
     */
    virtual Version version() const = 0;

    /**
     * Get the type of the server.
     *
     * @return Server type
     */
    virtual Type type() const = 0;

    /**
     * Get version string.
     *
     * @return Version string
     */
    virtual std::string version_string() const = 0;

    /*
     * Update server address. TODO: Move this to internal class once blr is gone.
     *
     * @param address       The new address
     */
    bool server_update_address(const std::string& address);

    /**
     * Update the server port. TODO: Move this to internal class once blr is gone.
     *
     * @param new_port New port. The value is not checked but should generally be 1 -- 65535.
     */
    void update_port(int new_port);

    /**
     * Update the server extra port. TODO: Move this to internal class once blr is gone.
     *
     * @param new_port New port. The value is not checked but should generally be 1 -- 65535.
     */
    void update_extra_port(int new_port);

    /**
     * @brief Check if a server points to a local MaxScale service
     *
     * @return True if the server points to a local MaxScale service
     */
    bool is_mxs_service();

    /**
     * Is the server valid and active? TODO: Rename once "is_active" is moved to internal class.
     *
     * @return True if server has not been removed from the runtime configuration.
     */
    bool server_is_active() const
    {
        return active();
    }

    bool active() const override
    {
        return is_active;
    }

    uint64_t status() const override
    {
        return m_status;
    }

    int64_t replication_lag() const override
    {
        return rlag;
    }

    int64_t ping() const override;

    void set_ping(int64_t ping);

    uint64_t capabilities() const override
    {
        return 0;
    }

    /**
     * Find a server with the specified name.
     *
     * @param name Name of the server
     * @return The server or NULL if not found
     */
    static SERVER* find_by_unique_name(const std::string& name);

    /**
     * Find several servers with the names specified in an array. The returned array is equal in size
     * to the server_names-array. If any server name was not found, then the corresponding element
     * will be NULL.
     *
     * @param server_names An array of server names
     * @return Array of servers
     */
    static std::vector<SERVER*> server_find_by_unique_names(const std::vector<std::string>& server_names);

    /**
     * Convert a status string to a status bit. Only converts one status element.
     *
     * @param str   String representation
     * @return bit value or 0 on error
     */
    static uint64_t status_from_string(const char* str);

    /**
     * Set a status bit in the server without locking
     *
     * @param bit           The bit to set for the server
     */
    void set_status(uint64_t bit);

    /**
     * Clear a status bit in the server without locking
     *
     * @param bit           The bit to clear for the server
     */
    void clear_status(uint64_t bit);

    // Assigns the status
    void assign_status(uint64_t status)
    {
        m_status = status;
    }

    const mxs::SSLProvider& ssl() const
    {
        return m_ssl_provider;
    }

    mxs::SSLProvider& ssl()
    {
        return m_ssl_provider;
    }

    void set_variables(std::unordered_map<std::string, std::string>&& variables)
    {
        std::lock_guard<std::mutex> guard(m_var_lock);
        m_variables = variables;
    }

    std::string get_variable(const std::string& key) const
    {
        std::lock_guard<std::mutex> guard(m_var_lock);
        auto it = m_variables.find(key);
        return it == m_variables.end() ? "" : it->second;
    }

    /**
     * Set GTID positions
     *
     * @param positions List of pairs for the domain and the GTID position for it
     */
    void set_gtid_list(const std::vector<std::pair<uint32_t, uint64_t>>& positions);

    /**
     * Remove all stored GTID positions
     */
    void clear_gtid_list();

    uint64_t gtid_pos(uint32_t domain) const override;

protected:
    SERVER(std::unique_ptr<mxs::SSLContext> ssl_context)
        : m_ssl_provider{std::move(ssl_context)}
    {
    }

private:
    static const int DEFAULT_CHARSET {0x08};        /**< The latin1 charset */
    mxs::SSLProvider m_ssl_provider;
    uint64_t         m_status {0};

    /** Server ping measured by monitor, in microseconds */
    std::atomic<int64_t> m_ping {mxs::Target::PING_UNDEFINED};

    // Server side global variables
    std::unordered_map<std::string, std::string> m_variables;
    // Lock that protects m_variables
    mutable std::mutex m_var_lock;

    struct GTID
    {
        std::atomic<int64_t>  domain{-1};
        std::atomic<uint64_t> sequence{0};
    };

    mxs::WorkerGlobal<std::unordered_map<uint32_t, uint64_t>> m_gtids;
};

/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <mutex>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <maxscale/config2.hh>
#include <maxscale/ssl.hh>
#include <maxscale/target.hh>

/**
 * The SERVER structure defines a backend server. Each server has a name
 * or IP address for the server, a port that the server listens on and
 * the name of a protocol module that is loaded to implement the protocol
 * between the gateway and the server.
 */
class SERVER : public mxs::Target
{
public:

    enum class BaseType {MARIADB, POSTGRESQL};

    /**
     * Stores server version info. Encodes/decodes to/from the version number received from the server.
     * Also stores the version string and parses information from it. Assumed to rarely change, so reads
     * are not synchronized. */
    class VersionInfo
    {
    public:
        enum class Type
        {
            UNKNOWN,    /**< Not connected yet */
            MYSQL,      /**< MySQL 5.5 or later. */
            MARIADB,    /**< MariaDB 5.5 or later */
            XPAND,      /**< Xpand node */
            BLR,        /**< Binlog router */
            POSTGRESQL, /**< PostgreSQL */
        };

        struct Version
        {
            uint64_t total {0};     /**< Total version number received from server */
            uint32_t major {0};     /**< Major version */
            uint32_t minor {0};     /**< Minor version */
            uint32_t patch {0};     /**< Patch version */
        };

        /**
         * Reads in version data. Deduces server type from version string.
         *
         * @param base_type      MariaDB or Pg
         * @param version_num    Version number from server
         * @param version_string Version string from server
         * @param caps           Server capabilities
         * @return True if version data changed
         */
        bool set(BaseType base_type, uint64_t version_num, const std::string& version_string, uint64_t caps);

        /**
         * Return true if the server is a real database and can process queries. Returns false if server
         * type is unknown or if the server is a binlogrouter.
         *
         * @return True if server is a real database
         */
        bool is_database() const;

        Type           type() const;
        const Version& version_num() const;
        const char*    version_string() const;
        std::string    type_string() const;
        uint64_t       capabilities() const;

    private:
        static const int MAX_VERSION_LEN = 256;

        mutable std::mutex m_lock;      /**< Protects against concurrent writing */

        Version  m_version_num;         /**< Numeric version */
        Type     m_type {Type::UNKNOWN};/**< Server type */
        uint64_t m_caps {0};

        char m_version_str[MAX_VERSION_LEN + 1] {'\0'};     /**< Server version string */
    };

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

    virtual ~SERVER() = default;

    /**
     * Get server address
     */
    virtual const char* address() const = 0;

    /**
     * Get server port
     */
    virtual int port() const = 0;

    /**
     * Get server extra port
     */
    virtual int extra_port() const = 0;

    /**
     * Is proxy protocol in use?
     */
    virtual bool proxy_protocol() const = 0;

    /**
     * Set proxy protocol
     *
     * @param proxy_protocol Whether proxy protocol is used
     */
    virtual void set_proxy_protocol(bool proxy_protocol) = 0;

    /**
     * Get server character set
     *
     * @return The numeric character set or 0 if no character set has been read
     */
    virtual uint8_t charset() const = 0;

    /**
     * Set server character set
     *
     * @param charset Character set to set
     */
    virtual void set_charset(uint8_t charset) = 0;

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
     * Update server version.
     *
     * @param version_num New numeric version
     * @param version_str New version string
     * @param caps        Server capabilities
     */
    virtual void set_version(BaseType base_type, uint64_t version_num, const std::string& version_str,
                             uint64_t caps) = 0;

    /**
     * Get version information. The contents of the referenced object may change at any time,
     * although in practice this is rare.
     *
     * @return Version information
     */
    virtual const VersionInfo& info() const = 0;

    /**
     * Update server address.
     *
     * @param address       The new address
     */
    virtual bool set_address(const std::string& address) = 0;

    /**
     * Update the server port.
     *
     * @param new_port New port. The value is not checked but should generally be 1 -- 65535.
     */
    virtual void set_port(int new_port) = 0;

    /**
     * Update the server extra port.
     *
     * @param new_port New port. The value is not checked but should generally be 1 -- 65535.
     */
    virtual void set_extra_port(int new_port) = 0;

    /**
     * @brief Check if a server points to a local MaxScale service
     *
     * @return True if the server points to a local MaxScale service
     */
    virtual bool is_mxs_service() const = 0;

    /**
     * Set current ping
     *
     * @param ping Ping in milliseconds
     */
    virtual void set_ping(int64_t ping) = 0;

    /**
     * Set replication lag
     *
     * @param lag The current replication lag in seconds
     */
    virtual void set_replication_lag(int64_t lag) = 0;

    // TODO: Don't expose this to the modules and instead destroy the server
    //       via ServerManager (currently needed by xpandmon)
    virtual void deactivate() = 0;

    virtual std::string monitor_user() const = 0;
    virtual std::string monitor_password() const = 0;

    /**
     * Set a status bit in the server without locking
     *
     * @param bit           The bit to set for the server
     */
    virtual void set_status(uint64_t bit) = 0;

    /**
     * Clear a status bit in the server without locking
     *
     * @param bit           The bit to clear for the server
     */
    virtual void clear_status(uint64_t bit) = 0;

    /**
     * Assign server status
     *
     * @param status Status to assign
     */
    virtual void assign_status(uint64_t status) = 0;

    /**
     * Get SSL configuration
     */
    virtual mxb::SSLConfig ssl_config() const = 0;

    /**
     * Track value of server variable.
     *
     * @param variable  The variable to track. It will as quoted be used in a
     *                  'SHOW GLOBAL VARIABLES WHERE VARIABLE_NAME IN (...)'
     *                  statement, so should be just the name without quotes.
     *
     * @return @c True, if the variable was added to the variables to be
     *         tracked, @c false if it was already present.
     */
    virtual bool track_variable(std::string variable) = 0;

    /**
     * Stop tracking the value of server variable.
     *
     * @param variable  The variable to track. Should be exactly like it was
     *                  when @c track_variable() was called.
     *
     * @return   @c True, if the variable was really removed, @c false if it
     *           was not present.
     */
    virtual bool untrack_variable(std::string variable) = 0;

    /**
     * Tracked variables.
     *
     * @return  The currently tracked variables.
     */
    virtual std::set<std::string> tracked_variables() const = 0;

    using Variables = std::map<std::string, std::string>;
    /**
     * Returns a map of server variables and their values. The content
     * of the map depends upon which variables the relevant monitor was
     * instructed to fetch. Note that @c session_track_system_variables
     * that is always fetched, is not returned in this map but by
     * @c get_session_track_system_variables().
     *
     * @return Map of server variables and their values.
     */
    virtual Variables get_variables() const = 0;

    /**
     * Get value of particular variable.
     *
     * @param variable  The variable.
     *
     * @return  The variable's value. Empty string if it has not been fetched.
     */
    virtual std::string get_variable_value(const std::string& variable) const = 0;

    /**
     * Set the variables as fetched from the MariaDB server. Should
     * be called only by the monitor.
     *
     * @param variables  The fetched variables and their values.
     */
    virtual void set_variables(Variables&& variables) = 0;

    /**
     * Set server uptime
     *
     * @param uptime Uptime in seconds
     */
    virtual void set_uptime(int64_t uptime) = 0;

    /**
     * Get server uptime
     *
     * @return Uptime in seconds
     */
    virtual int64_t get_uptime() const = 0;

    /**
     * Set GTID positions
     *
     * @param positions List of pairs for the domain and the GTID position for it
     */
    virtual void set_gtid_list(const std::vector<std::pair<uint32_t, uint64_t>>& positions) = 0;

    /**
     * Remove all stored GTID positions
     */
    virtual void clear_gtid_list() = 0;

    /**
     * Get current server priority
     *
     * This should be used to decide which server is chosen as a master. Currently only galeramon uses it.
     */
    virtual int64_t priority() const = 0;

    /**
     * Convert the configuration into parameters
     *
     * @return The server configuration
     */
    virtual mxs::ConfigParameters to_params() const = 0;

    /**
     * @return The configuration of the server.
     */
    virtual mxs::config::Configuration& configuration() = 0;

    /**
     * Set the server into maintenance mode.
     */
    virtual void set_maintenance() = 0;
};

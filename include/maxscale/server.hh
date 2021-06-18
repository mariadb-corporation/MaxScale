/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
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
#include <maxscale/config_common.hh>
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
            BLR         /**< Binlog router */
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
         * @param version_num Version number from server
         * @param version_string Version string from server
         * @return True if version data changed
         */
        bool set(uint64_t version_num, const std::string& version_string);

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

    private:
        static const int MAX_VERSION_LEN = 256;

        mutable std::mutex m_lock;      /**< Protects against concurrent writing */

        Version m_version_num;          /**< Numeric version */
        Type    m_type {Type::UNKNOWN}; /**< Server type */

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
     */
    virtual void set_version(uint64_t version_num, const std::string& version_str) = 0;

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
     * Set value of 'session_track_system_variables'.
     *
     * @param value Value found in the MariaDB Server
     */
    virtual void set_session_track_system_variables(std::string&& value) = 0;

    /**
     * Get server variable 'session_track_system_variables'.
     *
     * @param key Variable name to get
     * @return Variable value
     */
    virtual std::string get_session_track_system_variables() const = 0;

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
};

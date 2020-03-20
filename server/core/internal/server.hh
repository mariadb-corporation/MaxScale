/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
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
#include <maxscale/config.hh>
#include <maxscale/server.hh>
#include <maxscale/resultset.hh>

// Private server implementation
class Server : public SERVER
{
public:
    Server(const std::string& name, std::unique_ptr<mxs::SSLContext> ssl = {})
        : SERVER(std::move(ssl))
        , m_name(name)
    {
    }

    ~Server();

    const char* address() const override
    {
        return m_settings.address;
    }

    int port() const override
    {
        return m_settings.port;
    }

    int extra_port() const override
    {
        return m_settings.extra_port;
    }

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

    void set_rank(int64_t rank)
    {
        m_settings.rank = rank;
    }

    void set_priority(int64_t priority)
    {
        m_settings.priority = priority;
    }

    bool have_disk_space_limits() const override
    {
        std::lock_guard<std::mutex> guard(m_settings.lock);
        return !m_settings.disk_space_limits.empty();
    }

    DiskSpaceLimits get_disk_space_limits() const override
    {
        std::lock_guard<std::mutex> guard(m_settings.lock);
        return m_settings.disk_space_limits;
    }

    void set_disk_space_limits(const DiskSpaceLimits& new_limits)
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
        return m_info.version_num();
    }

    Type type() const override
    {
        return m_info.type();
    }

    std::string version_string() const override
    {
        return m_info.version_string();
    }

    const char* name() const override
    {
        return m_name.c_str();
    }

    int64_t rank() const override
    {
        return m_settings.rank;
    }

    int64_t priority() const override
    {
        return m_settings.priority;
    }

    /**
     * Print server details to a dcb.
     *
     * @param dcb Dcb to print to
     */
    void print_to_dcb(DCB* dcb) const;

    /**
     * Allocates a new server. Should only be called from ServerManager::create_server().
     *
     * @param name Server name
     * @param params Configuration
     * @return The new server or NULL on error
     */
    static Server* server_alloc(const char* name, const mxs::ConfigParameters& params);

    /**
     * Creates a server without any configuration. This should be used in unit tests in place of
     * a default ctor.
     *
     * @return A new server
     */
    static Server* create_test_server();

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

    void set_parameter(const std::string& name, const std::string& value);

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

    /**
     * Update server-specific monitor username. Does not affect existing monitor connections,
     * only new connections will use the updated username.
     *
     * @param username New username. Must not be too long.
     * @return True, if value was updated
     */
    bool set_monitor_user(const std::string& user);

    /**
     * Update server-specific monitor password. Does not affect existing monitor connections,
     * only new connections will use the updated password.
     *
     * @param password New password. Must not be too long.
     * @return True, if value was updated
     */
    bool set_monitor_password(const std::string& password);

    std::string monitor_user() const;
    std::string monitor_password() const;

    /**
     * @brief Set the disk space threshold of the server
     *
     * @param server                The server.
     * @param disk_space_threshold  The disk space threshold as specified in the config file.
     *
     * @return True, if the provided string is valid and the threshold could be set.
     */
    bool set_disk_space_threshold(const std::string& disk_space_threshold);

    /**
     * Print details of an individual server
     */
    void printServer();

    /**
     * Convert server to json. This does not add relations to other objects and should only be called from
     * ServerManager::server_to_json_data_relations().
     *
     * @param host Hostname of this server
     * @return Server as json
     */
    json_t* to_json_data(const char* host) const;

    json_t* json_attributes() const;

    std::unique_ptr<mxs::Endpoint> get_connection(mxs::Component* upstream, MXS_SESSION* session) override;

    const std::vector<mxs::Target*>& get_children() const override
    {
        static std::vector<mxs::Target*> no_children;
        return no_children;
    }

    bool set_address(const std::string& address) override;

    void set_port(int new_port) override;

    void set_extra_port(int new_port) override;


    uint64_t status() const override;
    void     set_status(uint64_t bit) override;
    void     clear_status(uint64_t bit) override;
    void     assign_status(uint64_t status) override;

    BackendDCB** persistent = nullptr;      /**< List of unused persistent connections to the server */

private:
    bool create_server_config(const char* filename) const;

    struct Settings
    {
        mutable std::mutex lock;    /**< Protects array-like settings from concurrent access */

        /** All config settings in text form. This is only read and written from the admin thread
         *  so no need for locking. */
        mxs::ConfigParameters all_parameters;

        std::string protocol;       /**< Backend protocol module name. Does not change so needs no locking. */

        char address[MAX_ADDRESS_LEN + 1] = {'\0'}; /**< Server hostname/IP-address */
        int  port = -1;                             /**< Server port */
        int  extra_port = -1;                       /**< Alternative monitor port if normal port fails */

        char monuser[MAX_MONUSER_LEN + 1] = {'\0'}; /**< Monitor username, overrides monitor setting */
        char monpw[MAX_MONPW_LEN + 1] = {'\0'};     /**< Monitor password, overrides monitor setting */

        long persistpoolmax = 0;    /**< Maximum size of persistent connections pool */
        long persistmaxtime = 0;    /**< Maximum number of seconds connection can live */

        int64_t rank;   /*< The ranking of this server, used to prioritize certain servers over others */

        int64_t priority;   /*< The priority of this server, Currently only used by galeramon to pick which
                             * server is the master. */

        /** Disk space thresholds. Can be queried from modules at any time so access must be protected
         *  by mutex. */
        DiskSpaceLimits disk_space_limits;
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

        Version     version_num() const;
        Type        type() const;
        std::string version_string() const;

    private:
        mutable std::mutex m_lock;      /**< Protects against concurrent writing */

        Version m_version_num;                              /**< Numeric version */
        Type    m_type = Type::MARIADB;                     /**< Server type */
        char    m_version_str[MAX_VERSION_LEN + 1] = {'\0'};/**< Server version string */
    };

    const std::string m_name;               /**< Server config name */
    Settings          m_settings;           /**< Server settings */
    VersionInfo       m_info;               /**< Server version and type information */
    uint64_t          m_status {0};
};

// A connection to a server
class ServerEndpoint final : public mxs::Endpoint
{
public:
    ServerEndpoint(mxs::Component* up, MXS_SESSION* session, Server* server);
    ~ServerEndpoint() override;

    mxs::Target* target() const override;

    bool connect() override;

    void close() override;

    bool is_open() const override;

    int32_t routeQuery(GWBUF* buffer) override;

    int32_t clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    bool handleError(mxs::ErrorType type, GWBUF* error, mxs::Endpoint* down,
                     const mxs::Reply& reply) override;

private:
    DCB*            m_dcb {nullptr};
    mxs::Component* m_up;
    MXS_SESSION*    m_session;
    Server*         m_server;
};

/**
 * Returns parameter definitions shared by all servers.
 *
 * @return Common server parameters.
 */
const MXS_MODULE_PARAM* common_server_params();

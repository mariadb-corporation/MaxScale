/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
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
#include <maxscale/config2.hh>
#include <maxscale/server.hh>
#include <maxscale/workerlocal.hh>

// Private server implementation
class Server : public SERVER
{
public:
    friend class ServerManager;

    static const int MAX_ADDRESS_LEN = 1024;
    static const int MAX_MONUSER_LEN = 512;
    static const int MAX_MONPW_LEN = 512;

    class ParamDiskSpaceLimits : public mxs::config::ConcreteParam<ParamDiskSpaceLimits
                                                                   , DiskSpaceLimits>
    {
    public:
        ParamDiskSpaceLimits(mxs::config::Specification* pSpecification,
                             const char* zName, const char* zDescription);
        std::string type() const override;
        std::string to_string(value_type value) const;
        bool        from_string(const std::string& value, value_type* pValue,
                                std::string* pMessage = nullptr) const;
        json_t* to_json(value_type value) const;
        bool    from_json(const json_t* pJson, value_type* pValue, std::string* pMessage = nullptr) const;
    };

    class ParamSSL : public mxs::config::ConcreteParam<ParamSSL, bool>
    {
    public:
        ParamSSL(mxs::config::Specification* pSpecification, const char* zName, const char* zDescription);
        std::string type() const override;
        std::string to_string(value_type value) const;
        bool        from_string(const std::string& value, value_type* pValue,
                                std::string* pMessage = nullptr) const;
        json_t* to_json(value_type value) const;
        bool    from_json(const json_t* pJson, value_type* pValue, std::string* pMessage = nullptr) const;
    };

    Server(const std::string& name, std::unique_ptr<mxs::SSLContext> ssl = {})
        : m_name(name)
        , m_settings(name)
        , m_ssl_provider(std::move(ssl))
    {
    }

    ~Server() = default;

    const char* address() const override
    {
        return m_settings.address;
    }

    int port() const override
    {
        return m_settings.m_port.get();
    }

    int extra_port() const override
    {
        return m_settings.m_extra_port.get();
    }

    long persistpoolmax() const
    {
        return m_settings.m_persistpoolmax.get();
    }

    void set_persistpoolmax(long persistpoolmax)
    {
        m_settings.m_persistpoolmax.set(persistpoolmax);
    }

    long persistmaxtime() const
    {
        return m_settings.m_persistmaxtime.get().count();
    }

    void set_persistmaxtime(long persistmaxtime)
    {
        m_settings.m_persistmaxtime.set(std::chrono::seconds(persistmaxtime));
    }

    void set_rank(int64_t rank)
    {
        m_settings.m_rank.set(rank);
    }

    void set_priority(int64_t priority)
    {
        m_settings.m_priority.set(priority);
    }

    bool have_disk_space_limits() const override
    {
        return m_settings.m_have_disk_space_limits.load(std::memory_order_relaxed);
    }

    DiskSpaceLimits get_disk_space_limits() const override
    {
        return m_settings.m_disk_space_threshold.get();
    }

    bool persistent_conns_enabled() const override
    {
        return m_settings.m_persistpoolmax.get() > 0;
    }

    void set_version(uint64_t version_num, const std::string& version_str) override;

    const VersionInfo& info() const override;

    const char* name() const override
    {
        return m_name.c_str();
    }

    int64_t rank() const override
    {
        return m_settings.m_rank.get();
    }

    int64_t priority() const override
    {
        return m_settings.m_priority.get();
    }

    /**
     * Print server details to a dcb.
     *
     * @param dcb Dcb to print to
     */
    void print_to_dcb(DCB* dcb) const;

    /**
     * Creates a server without any configuration. This should be used in unit tests in place of
     * a default ctor.
     *
     * @return A new server
     */
    static Server* create_test_server();

    /**
     * Get server configuration specification
     */
    static const mxs::config::Specification& specification();

    /**
     * Configure the server
     *
     * Must be done in the admin thread.
     *
     * @param params New parameters that have been validated
     *
     * @return True if the configuration was successful
     */
    bool configure(const mxs::ConfigParameters& params);

    /**
     * Configure the server from JSON
     *
     * Must be done in the admin thread.
     *
     * @param params JSON parameters that have been validated
     *
     * @return True if the configuration was successful
     */
    bool configure(json_t* json);

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
     * Persist server configuration into a stream
     *
     * @param filename Stream where the configuration is written
     *
     * @return The output stream
     */
    std::ostream& persist(std::ostream& os) const
    {
        return m_settings.persist(os);
    }

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
     * Convert a status string to a status bit. Only converts one status element.
     *
     * @param str   String representation
     * @return bit value or 0 on error
     */
    static uint64_t status_from_string(const char* str);

    json_t* json_attributes() const;

    std::unique_ptr<mxs::Endpoint> get_connection(mxs::Component* upstream, MXS_SESSION* session) override;

    const std::vector<mxs::Target*>& get_children() const override
    {
        static std::vector<mxs::Target*> no_children;
        return no_children;
    }

    uint64_t capabilities() const override
    {
        return 0;
    }

    bool active() const override
    {
        return m_active;
    }

    void deactivate() override
    {
        assign_status(0);
        m_active = false;
    }

    int64_t replication_lag() const override
    {
        return m_rpl_lag;
    }

    void set_replication_lag(int64_t lag) override
    {
        m_rpl_lag = lag;
    }

    int64_t ping() const override
    {
        return m_ping;
    }

    void set_ping(int64_t ping) override
    {
        m_ping = ping;
    }

    bool set_address(const std::string& address) override;

    void set_port(int new_port) override;

    void set_extra_port(int new_port) override;

    uint64_t status() const override;
    void     set_status(uint64_t bit) override;
    void     clear_status(uint64_t bit) override;
    void     assign_status(uint64_t status) override;

    const mxs::SSLProvider& ssl() const override;
    mxs::SSLProvider&       ssl() override;

    void        set_session_track_system_variables(std::string&& value) override;
    std::string get_session_track_system_variables() const override;

    uint64_t gtid_pos(uint32_t domain) const override;
    void     set_gtid_list(const std::vector<std::pair<uint32_t, uint64_t>>& positions) override;
    void     clear_gtid_list() override;

    uint8_t    charset() const override;
    void       set_charset(uint8_t charset) override;
    bool       proxy_protocol() const override;
    void       set_proxy_protocol(bool proxy_protocol) override;
    PoolStats& pool_stats();
    bool       is_mxs_service() const override;

private:
    bool create_server_config(const char* filename) const;

    /**
     * Allocates a new server. Can only be called from ServerManager::create_server().
     *
     * @param name   Server name
     * @param params Configuration
     *
     * @return The new server if creation was successful
     */
    static std::unique_ptr<Server> create(const char* name, const mxs::ConfigParameters& params);
    static std::unique_ptr<Server> create(const char* name, json_t* json);

    /**
     * Convert server to json. This does not add relations to other objects and can only be called from
     * ServerManager::server_to_json_data_relations().
     *
     * @param host Hostname of this server
     * @return Server as json
     */
    json_t* to_json_data(const char* host) const;

    struct Settings : public mxs::config::Configuration
    {
        Settings(const std::string& name);

        char address[MAX_ADDRESS_LEN + 1] = {'\0'}; /**< Server hostname/IP-address */
        char monuser[MAX_MONUSER_LEN + 1] = {'\0'}; /**< Monitor username, overrides monitor setting */
        char monpw[MAX_MONPW_LEN + 1] = {'\0'};     /**< Monitor password, overrides monitor setting */

        // Used to track whether disk space limits are enabled. Avoids the lock acquisition on the value.
        std::atomic<bool> m_have_disk_space_limits {false};

        // Module type, always "server"
        mxs::config::String m_type;
        // Module protocol, deprecated
        mxs::config::String m_protocol;
        // Authenticator module, deprecated
        mxs::config::String m_authenticator;
        // The server address, mutually exclusive with socket. @see address
        mxs::config::String m_address;
        // The server socket, mutually exclusive with address
        mxs::config::String m_socket;
        // Server port
        mxs::config::Count m_port;
        // Alternative monitor port if normal port fails
        mxs::config::Count m_extra_port;
        // The priority of this server, Currently only used by galeramon to pick which server is the master
        mxs::config::Count m_priority;
        // @see monuser
        mxs::config::String m_monitoruser;
        // @see monpw
        mxs::config::String m_monitorpw;
        // Maximum size of persistent connections pool
        mxs::config::Count m_persistpoolmax;
        // Maximum number of seconds connection can live
        mxs::config::Seconds m_persistmaxtime;
        // Send proxy-protocol header to backends when connecting routing sessions
        mxs::config::Bool m_proxy_protocol;
        // Disk space limits
        mxs::config::ConcreteType<ParamDiskSpaceLimits> m_disk_space_threshold;
        // The ranking of this server, used to prioritize certain servers over others during routing
        mxs::config::Enum<int64_t> m_rank;

        // TLS configuration parameters
        mxs::config::ConcreteType<ParamSSL> m_ssl;
        mxs::config::Path                   m_ssl_cert;
        mxs::config::Path                   m_ssl_key;
        mxs::config::Path                   m_ssl_ca;

        mxs::config::Enum<mxb::ssl_version::Version> m_ssl_version;

        mxs::config::Count  m_ssl_cert_verify_depth;
        mxs::config::Bool   m_ssl_verify_peer_certificate;
        mxs::config::Bool   m_ssl_verify_peer_host;
        mxs::config::String m_ssl_cipher;

    protected:
        bool post_configure() override;
    };

    const std::string m_name;       /**< Server config name */
    Settings          m_settings;   /**< Server settings */
    VersionInfo       m_info;       /**< Server version and type information */
    uint64_t          m_status {0};
    bool              m_active {true};
    int64_t           m_rpl_lag {mxs::Target::RLAG_UNDEFINED};  /**< Replication lag in seconds */
    int64_t           m_ping {mxs::Target::PING_UNDEFINED};     /**< Ping in microseconds */
    mxs::SSLProvider  m_ssl_provider;
    PoolStats         m_pool_stats;

    // Character set. Read from backend and sent to client. As no character set has the numeric value of 0, it
    // can be used to detect servers we haven't connected to.
    uint8_t m_charset = 0;

    // Latest value of server global variable 'session_track_system_variables'. Updated every 10min
    std::string m_session_track_system_variables;
    // Lock that protects m_variables
    mutable std::mutex m_var_lock;

    struct GTID
    {
        std::atomic<int64_t>  domain {-1};
        std::atomic<uint64_t> sequence {0};
    };

    mxs::WorkerGlobal<std::unordered_map<uint32_t, uint64_t>> m_gtids;

    /**
     * @brief Clean up any stale persistent connections
     *
     * This function purges any stale persistent connections from @c server.
     *
     * @param server Server to clean up
     */
    void cleanup_persistent_connections() const;

    bool configure_ssl(const mxs::ConfigParameters& params);
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

/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <unordered_map>
#include <maxbase/proxy_protocol.hh>
#include <maxscale/authenticator.hh>
#include <maxscale/buffer.hh>
#include <maxscale/connection_metadata.hh>
#include <maxscale/ssl.hh>
#include <maxscale/workerlocal.hh>

class SERVICE;
namespace maxscale
{
class ProtocolModule;

/**
 * Listener settings and other data that is shared with all sessions created by the listener.
 * Should be referred to with shared_ptr.
 *
 * The contents should not change once a session with the data has been created, as this could
 * create concurrency issues. If listener settings are changed, the listener should create a new
 * shared data object and share that with new sessions. The old sessions will keep using the
 * previous settings.
 */
class ListenerData
{
public:
    using SProtocol = std::unique_ptr<mxs::ProtocolModule>;
    using SAuthenticator = std::unique_ptr<mxs::AuthenticatorModule>;

    struct ConnectionInitSql
    {
        std::vector<std::string> queries;
        GWBUF                    buffer_contents;
    };

    struct UserCreds
    {
        std::string password;
        std::string plugin;
    };

    struct MappingInfo
    {
        std::unordered_map<std::string, std::string> user_map;      /**< user -> user */
        std::unordered_map<std::string, std::string> group_map;     /**< Linux group -> user */
        std::unordered_map<std::string, UserCreds>   credentials;   /**< user -> plugin & pw */
    };
    using SMappingInfo = std::unique_ptr<const MappingInfo>;

    ListenerData() = default;
    ListenerData(SSLContext ssl, mxs::Parser::SqlMode default_sql_mode,
                 SProtocol protocol_module, const std::string& listener_name,
                 std::vector<SAuthenticator>&& authenticators, ConnectionInitSql&& init_sql,
                 SMappingInfo mapping, mxb::proxy_protocol::SubnetArray&& proxy_networks);

    ListenerData(const ListenerData&) = delete;
    ListenerData& operator=(const ListenerData&) = delete;
    ListenerData(ListenerData&&) = default;
    ListenerData& operator=(ListenerData&&) = default;

    const SSLContext m_ssl;                     /**< SSL settings */
    // Default sql mode for the listener
    const mxs::Parser::SqlMode m_default_sql_mode{mxs::Parser::SqlMode::DEFAULT};
    const SProtocol            m_proto_module;          /**< Protocol module */
    const std::string          m_listener_name;         /**< Name of the owning listener */

    /**
     * Authenticator modules used by the sessions created from the listener. The session will select
     * an authenticator module during authentication.
     */
    const std::vector<SAuthenticator> m_authenticators;

    /** Connection init sql queries. Only used by MariaDB-protocol module .*/
    const ConnectionInitSql m_conn_init_sql;

    const std::unique_ptr<const MappingInfo> m_mapping_info;    /**< Backend user mapping and passwords */
    const mxb::proxy_protocol::SubnetArray   m_proxy_networks;  /**< Allowed proxy protocol (sub)networks */
};

/**
 * The Listener class is used to link a network port to a service. It defines the name of the
 * protocol module that should be loaded as well as the authenticator that is used.
 */
class Listener;
using SListener = std::shared_ptr<Listener>;

class Listener : public mxb::Pollable
{
public:
    using SData = std::shared_ptr<const mxs::ListenerData>;
    using SMetadata = std::shared_ptr<const mxs::ConnectionMetadata>;

    enum class Type
    {
        UNIX_SOCKET,    // UNIX domain socket shared between workers
        SHARED_TCP,     // TCP listening socket shared between workers
        UNIQUE_TCP,     // Unique TCP listening socket for each worker
    };

    struct Config : public mxs::config::Configuration
    {
        Config(const std::string& name, Listener* listener);

        std::string              type;
        const MXS_MODULE*        protocol;
        std::string              authenticator;
        std::string              authenticator_options;
        std::string              address;
        std::string              socket;
        int64_t                  port;
        SERVICE*                 service;
        mxs::Parser::SqlMode     sql_mode;
        std::string              connection_init_sql_file;
        std::string              user_mapping_file;
        std::string              proxy_networks;
        std::vector<std::string> connection_metadata;

        // TLS configuration parameters
        bool        ssl;
        std::string ssl_cert;
        std::string ssl_key;
        std::string ssl_ca;
        std::string ssl_cipher;
        std::string ssl_crl;
        int64_t     ssl_cert_verify_depth;
        bool        ssl_verify_peer_certificate;
        bool        ssl_verify_peer_host;

        mxb::ssl_version::Version ssl_version;

        bool configure(const mxs::ConfigParameters& params,
                       mxs::ConfigParameters* pUnrecognized = nullptr) override final;

        bool configure(json_t* json, std::set<std::string>* pUnrecognized = nullptr) override final;

    protected:
        bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;

        Listener* m_listener;
    };

    virtual ~Listener() override;

    /**
     * Create a new listener
     *
     * @param name     Name of the listener
     * @param params   Parameters for the listener
     *
     * @return New listener or nullptr on error
     */
    static SListener create(const std::string& name, const mxs::ConfigParameters& params);
    static SListener create(const std::string& name, json_t* params);

    /**
     * Destroy a listener
     *
     * This removes the listener from the global list of active listeners. Once destroyed, the port used
     * by a listener is open for immediate reuse.
     *
     * @param listener Listener to destroy
     */
    static void destroy(const SListener& listener);

    /**
     * Removes all listeners. If there are no external references to
     * a listener, it will be deleted as well.
     *
     * @note Does not call @c destroy on each instance and an existing
     *       shared_ptr to a listener continues to be valid.
     */
    static void clear();

    /**
     * Find a listener
     *
     * @param name Name of the listener
     *
     * @return The listener if it exists or an empty SListener if it doesn't
     */
    static SListener find(const std::string& name);

    /**
     * Find all listeners that point to a service
     *
     * @param service Service whose listeners are returned
     *
     * @return The listeners that point to the service
     */
    static std::vector<SListener> find_by_service(const SERVICE* service);

    // Stop all listeners
    static void stop_all();

    /**
     * Reloads TLS certificates for all listeners
     *
     * @return True if certificate reload succeeded on all listeners
     */
    static bool reload_tls();

    /**
     * @note Only to be called from the main worker.
     *
     * @return All listeners that have been started.
     */
    static std::vector<SListener> get_started_listeners();

    /**
     * Increment the number of authentication failures from the remote address. If the number
     * exceeds the configured limit, future attempts to connect from the remote are be rejected.
     *
     * @param remote The address where the connection originated
     */
    static void mark_auth_as_failed(const std::string& remote);

    /**
     * Called whenever a change in server variables was detected
     */
    static void server_variables_changed(SERVER* server);

    /**
     * Get listener config
     */
    mxs::config::Configuration& configuration()
    {
        return m_config;
    }

    /**
     * Start listening on the configured port
     *
     * @return True if the listener was able to start listening
     */
    bool listen();

    /**
     * Start listening at specified routing worker that was not present
     * when the listener was started.
     *
     * @note This function can only be called from the main worker or
     *       from @c worker provided as argument.
     *
     * @param worker  The routing worker where the listener should also listen.
     *
     * @return True if the listener could be added to the worker. True will
     *         unconditionally be returned if the listener is not started.
     */
    bool listen(mxs::RoutingWorker& worker);

    /**
     * Stop listening at specified routing worker.
     *
     * @note This function can only be called from the main worker or
     *       from @c worker provided as argument.
     *
     * @param worker  The routing worker that should stop listening.
     *
     * @return True if the listener could be removed from the worker. True will
     *         unconditionally be returned if the listener is not started.
     */
    bool unlisten(mxs::RoutingWorker& worker);

    /**
     * Stop the listener
     *
     * @return True if the listener was successfully stopped
     */
    bool stop();

    /**
     * Start a stopped listener
     *
     * @return True if the listener was successfully started
     */
    bool start();

    /**
     * Listener name
     */
    const char* name() const;

    /**
     * Network address the listener listens on
     */
    const char* address() const;

    /**
     * Network port the listener listens on
     */
    uint16_t port() const;

    /**
     * Service the listener points to
     */
    SERVICE* service() const;

    /**
     * The protocol module name
     */
    const char* protocol() const;

    /**
     * The state of the listener
     */
    const char* state() const;

    /**
     * The service that the listener points to
     */
    SERVICE* service()
    {
        return m_config.service;
    }

    /**
     * Convert to JSON
     *
     * @param host The hostname of this server
     *
     * @return JSON representation of the object
     */
    json_t* to_json(const char* host) const;

    /**
     * Get listener as a JSON API resource
     *
     * @param host The hostname of this server
     *
     * @return JSON API resource representation of the object
     */
    json_t* to_json_resource(const char* host) const;

    /**
     * Get all listeners as a JSON API resource collection
     *
     * @param host The hostname of this server
     *
     * @return The listeners resource collection
     */
    static json_t* to_json_collection(const char* host);

    Type type() const
    {
        return m_type;
    }

    /**
     * Persist listener configuration into a stream
     *
     * @param os Output stream where the listener is persisted
     *
     * @return The output stream given as the parameter
     */
    std::ostream& persist(std::ostream& os) const;

    bool post_configure(const mxs::ConfigParameters& protocol_params);

    /**
     * Create listener data object for test purposes. The parameters should still be valid listener
     * settings, as they are parsed normally. Returns a shared_ptr as that is typically used by tests.
     *
     * @param params Associated listener settings
     *
     * @return New listener data object for test sessions
     */
    static SData create_test_data(const mxs::ConfigParameters& params);

    static mxs::config::Specification* specification();

    mxb::SSLConfig ssl_config() const
    {
        return create_ssl_config();
    }

    // Pollable
    int poll_fd() const override;

private:
    class Manager;

    struct SharedData
    {
        SData     listener_data;
        SMetadata metadata;
    };

    enum State
    {
        CREATED,
        STARTED,
        STOPPED,
        FAILED,
        DESTROYED
    };

    /**
     * Creates a new listener that points to a service
     *
     * @param service       Service where the listener points to
     * @param name          Name of the listener
     * @param address       The address where the listener listens
     * @param port          The port on which the listener listens
     * @param protocol      The protocol module to use
     */
    Listener(const std::string& name);

    /**
     * Listen on a file descriptor shared between all workers
     *
     * @return True if the listening was started successfully
     */
    bool listen_shared();

    /**
     * Listen on a file descriptor at specified worker
     *
     * @param worker  The worker where the listener should also listen.
     *
     * @return True if the listening was started successfully
     */
    bool listen_shared(mxs::RoutingWorker& worker);

    /**
     * Listen with a unique file descriptor for each worker
     *
     * @return True if the listening was started successfully
     */
    bool listen_unique();

    /**
     * Listen with a unique file descriptor at specified worker
     *
     * @param worker  The worker where the listener should also listen.
     *
     * @return True if the listening was started successfully
     */
    bool listen_unique(mxs::RoutingWorker& worker);

    /**
     * Stop listening at specified worker.
     *
     * @param worker  The worker in question.
     *
     * @return True if the listening was stopped successfully.
     */
    bool unlisten_shared(mxs::RoutingWorker& worker);

    /**
     * Stop listening at specified worker.
     *
     * @param worker  The worker in question.
     *
     * @return True if the listening was stopped successfully.
     */
    bool unlisten_unique(mxs::RoutingWorker& worker);

    /**
     * Close all opened file descriptors for this listener
     */
    void close_all_fds();

    /**
     * Accept a single client connection
     *
     * @param fd          The opened file descriptor to which the client is connected to
     * @param addr        The network information
     * @param host        The host where the client is connecting from
     * @param shared_data The shared data of this listener
     *
     * @return The new DCB or nullptr on error
     */
    ClientDCB* accept_one_dcb(int fd, const sockaddr_storage* addr, const char* host,
                              const SharedData& shared_data);

    /**
     * Accept all available client connections
     */
    void accept_connections();

    /**
     * Reject a client connection
     *
     * Writes an error message to the fd if the protocol supports it and then closes it.
     *
     * @param fd   The file descriptor to close
     * @param host The host where the connection originated from
     */
    void reject_connection(int fd, const char* host);

    /**
     * The file descriptor for accepting new connections
     *
     * @return The worker-local file descriptor
     */
    int fd() const
    {
        return m_type == Type::UNIQUE_TCP ? *m_local_fd : m_shared_fd;
    }

    uint32_t handle_poll_events(mxb::Worker* worker, uint32_t events, Pollable::Context context) override;

    bool read_connection_init_sql(const mxs::ProtocolModule& protocol,
                                  ListenerData::ConnectionInitSql& output) const;

    bool           read_user_mapping(mxs::ListenerData::SMappingInfo& output) const;
    bool           read_proxy_networks(mxb::proxy_protocol::SubnetArray& output);
    SData          create_shared_data(const mxs::ConfigParameters& protocol_params);
    SMetadata      create_connection_metadata();
    mxb::SSLConfig create_ssl_config() const;
    void           set_type();
    json_t*        json_parameters() const;
    bool           force_config_reload();
    bool           open_unique_listener(mxs::RoutingWorker& worker, std::mutex& lock,
                                        std::vector<std::string>& errors);

    Config                m_config;         /**< The listener configuration */
    std::string           m_name;           /**< Name of the listener */
    State                 m_state;          /**< Listener state */
    mxs::ConfigParameters m_params;         /**< Not validated and only used to construct authenticators. */
    Type                  m_type;           /**< The type of the listener */
    mxs::WorkerLocal<int> m_local_fd {-1};  /**< File descriptor the listener listens on */
    int                   m_shared_fd {-1}; /**< File descriptor the listener listens on */

    mxs::WorkerGlobal<SharedData> m_shared_data;    /**< Data shared with sessions */

    static Manager s_manager;   /**< Manager of all listener instances */
};
}

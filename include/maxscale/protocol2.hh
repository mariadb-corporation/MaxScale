/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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
#include <maxscale/protocol.hh>
#include <maxscale/authenticator.hh>
#include <maxscale/config2.hh>

class BackendDCB;
class ClientDCB;
class DCB;
class SERVICE;

namespace maxscale
{
class BackendConnection;
class ClientConnection;
class ConfigParameters;
class Parser;
class UserAccountManager;

class ProtocolModule
{
public:
    using AuthenticatorList = std::vector<mxs::SAuthenticatorModule>;

    virtual ~ProtocolModule() = default;

    enum Capabilities
    {
        CAP_AUTHDATA     = (1u << 0),   // Protocol implements a user account manager
        CAP_BACKEND      = (1u << 1),   // Protocol supports backend communication
        CAP_AUTH_MODULES = (1u << 2),   // Protocol uses authenticator modules and does not integrate one
    };


    /**
     * Get the protocol module configuration
     *
     * The configure method of the returned configuration will be called after the initial creation of the
     * module as well as any time a parameter is modified at runtime.
     *
     * @return The configuration of the listener
     */
    virtual mxs::config::Configuration& getConfiguration() = 0;

    /**
     * Allocate new client protocol session
     *
     * @param session   The session to which the connection belongs to
     * @param component The component to use for routeQuery
     *
     * @return New protocol session or null on error
     */
    virtual std::unique_ptr<mxs::ClientConnection>
    create_client_protocol(MXS_SESSION* session, mxs::Component* component) = 0;

    /**
     * Allocate new backend protocol session
     *
     * @param session  The session to which the connection belongs to
     * @param server   Server where the connection is made
     *
     * @return New protocol session or null on error
     */
    virtual std::unique_ptr<BackendConnection>
    create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component)
    {
        mxb_assert(!true);
        return nullptr;
    }

    /**
     * Get the default authenticator for the protocol.
     *
     * @return The default authenticator for the protocol or empty if the protocol
     * does not provide one
     */
    virtual std::string auth_default() const = 0;

    /**
     * Create an error message
     *
     * The protocol should return an error with the given human-readable error message. Non-MariaDB protocols
     * can ignore the error number if the protocol does not have a concept of error numbers or no suitable
     * mapping is found.
     *
     * @param errnum   The MariaDB error code
     * @param sqlstate The SQLSTATE of the error
     * @param message  The message to send to the client.
     *
     * @return A buffer containing the error message
     */
    virtual GWBUF make_error(int errnum, const std::string& sqlstate, const std::string& message) const = 0;

    /**
     * If the packet contains SQL, return it as a string_view.
     *
     * @return Non-empty string_view if the packet contains SQL, an empty string_view otherwise.
     *
     * @note The returned string_view is valid only as long as @c packet is.
     */
    virtual std::string_view get_sql(const GWBUF& packet) const = 0;

    /**
     * Returns a human-readable description of the @c packet, which is assumed
     * to be a protocol packet obtained from a client connection created using
     * this protocol module.
     *
     * @param packet        A protocol packet received via a client connection
     *                      of this protocol module.
     * @param body_max_len  If the packet contains human readable data, the amount
     *                      of it that should be included.
     *
     * @return Human-readable string. May be empty.
     */
    virtual std::string describe(const GWBUF& packet, int body_max_len = 1000) const = 0;

    /**
     * Create query
     *
     * The protocol should return a packet that can be routed to a backend server which executes a SQL query.
     *
     * @param errnum   The SQL to execute
     *
     * @return A buffer containing an SQL query
     */
    virtual GWBUF make_query(std::string_view query) const
    {
        mxb_assert(!true);
        return GWBUF{};
    }

    /**
     * Get protocol module name.
     *
     * @return Module name
     */
    virtual std::string name() const = 0;

    /**
     * Get the name of the network protocol that this module implements
     *
     * The set of "registered" protocol names in MaxScale can be found in
     * `include/maxscale/protocols/.../module_names.hh`. Each protocol should
     * have a header that defines the network protocol name.
     */
    virtual std::string protocol_name() const = 0;

    /**
     * Print a list of authenticator users to json. This should only be implemented by protocols without
     * CAP_AUTHDATA.
     *
     * @return JSON user list
     */
    virtual json_t* print_auth_users_json()
    {
        return nullptr;
    }

    /**
     * Create a user account manager. Will be only called for protocols with CAP_AUTHDATA.
     *
     * @return New user account manager. Will be shared between all listeners of the service.
     */
    virtual std::unique_ptr<UserAccountManager> create_user_data_manager()
    {
        mxb_assert(!true);
        return nullptr;
    }

    virtual uint64_t capabilities() const
    {
        return 0;
    }

    /**
     * The protocol module should read the listener parameters for the list of authenticators and their
     * options and generate authenticator modules. This is only called if CAP_AUTH_MODULES is enabled.
     *
     * @param params Listener and authenticator settings
     * @return An array of authenticators. Empty on error.
     */
    virtual AuthenticatorList create_authenticators(const ConfigParameters& params)
    {
        mxb_assert(!true);
        return {};
    }
};

/**
 * Client protocol connection interface. All protocols must implement this.
 */
class ClientConnection : public ProtocolConnection
{
public:
    virtual ~ClientConnection() = default;

    /**
     * Initialize a connection.
     *
     * @return True, if the connection could be initialized, false otherwise.
     */
    virtual bool init_connection() = 0;

    /**
     * Finalize a connection. Called right before the DCB itself is closed.
     */
    virtual void finish_connection() = 0;

    /**
     * Handle connection limits. Currently the return value is ignored.
     *
     * @param limit Maximum number of connections
     * @return 1 on success, 0 on error
     */
    virtual int32_t connlimit(int limit)
    {
        return 0;
    }

    /**
     * Return current database. Only required by query classifier.
     *
     * @return Current database
     */
    virtual std::string current_db() const
    {
        return "";
    }

    /**
     * Route reply to client. This should be called from the session to route a query to client instead of
     * write(), as write() does not update routing status.
     *
     * @param buffer Reply buffer
     * @param down Path taken
     * @param reply Reply info
     * @return True on success
     */
    virtual bool clientReply(GWBUF&& buffer, ReplyRoute& down, const mxs::Reply& reply) = 0;

    virtual ClientDCB*       dcb() = 0;
    virtual const ClientDCB* dcb() const = 0;

    virtual void wakeup()
    {
        // Should not be called for non-supported protocols.
        mxb_assert(!true);
    }

    /**
     * Is the client protocol in routing state, that is, can data be delivered to
     * it for further delivery to the client.
     *
     * @return True, if in routing state, false otherwise.
     */
    virtual bool in_routing_state() const = 0;

    /**
     * Can the session be safely restarted?
     *
     * A session restart causes the router and filter sessions to be recreated which means backend connections
     * are also recreated. If the connection is in a state which cannot be safely restored, the implementation
     * for this should return false.
     *
     * @return Whether the session can be safely restarted
     */
    virtual bool safe_to_restart() const = 0;

    /**
     * Called when the session starts to stop
     *
     * This can be used to do any preparatory work that needs to be done before the actual shutdown is
     * started. At this stage the session is still valid and routing works normally.
     *
     * The default implementation does nothing.
     */
    virtual void kill()
    {
    }

    /**
     * Will be called during idle processing.
     *
     * @param idle  The number of seconds the connection has been idle.
     */
    virtual void tick(std::chrono::seconds idle)
    {
    }

    /**
     * Returns a parser appropriate for the protocol in question, or NULL if
     * there is not one.
     *
     * @return A parser appropriate for the protocol in question, or NULL.
     */
    const Parser* parser() const
    {
        return const_cast<ClientConnection*>(this)->parser();
    }

    virtual Parser* parser()
    {
        return nullptr;
    }
};

/**
 * Partial client protocol implementation. More fields and functions may be added later.
 */
class ClientConnectionBase : public ClientConnection
{
public:
    json_t*          diagnostics() const override;
    void             set_dcb(DCB* dcb) override;
    ClientDCB*       dcb() override;
    const ClientDCB* dcb() const override;

    bool in_routing_state() const override;

    size_t sizeof_buffers() const override = 0;

protected:
    ClientDCB* m_dcb {nullptr};     /**< Dcb used by this protocol connection */
};

/**
 * Backend protocol connection interface. Only protocols with backend support need to implement this.
 */
class BackendConnection : public mxs::ProtocolConnection
{
public:
    virtual ~BackendConnection() = default;

    static constexpr const uint64_t REUSE_NOT_POSSIBLE = 0;
    static constexpr const uint64_t OPTIMAL_REUSE = std::numeric_limits<uint64_t>::max();

    /**
     * Finalize a connection. Called right before the DCB itself is closed.
     */
    virtual void finish_connection() = 0;

    /**
     * Test if this connection can be reused by the session
     *
     * The protocol can have limitations that prevent it from being reused with some sessions. Mainly these
     * are caused by connection level differences that cannot be changed once it has been established.
     *
     * @param session The session to check compatibility for
     *
     * @return A number representing how well this connection matches. A larger number represents a better
     * candidate for reuse. To stop the search early, the function should return the OPTIMAL_REUSE constant to
     * indicate that the best candidate was found. If a connection cannot be reused, the function should
     * return the REUSE_NOT_POSSIBLE constant.
     */
    virtual uint64_t can_reuse(MXS_SESSION* session) const = 0;

    /**
     * Reuse a connection. The connection was in the persistent pool
     * and will now be taken into use again.
     *
     * @param session    The session to attach to.
     * @param upstream   The upstream component.
     * @param reuse_type The value returned by the can_reuse method.
     *
     * @return True, if the connection can be reused, false otherwise.
     *         If @c false is returned, the connection should be closed.
     */
    virtual bool reuse(MXS_SESSION* session, mxs::Component* upstream, uint64_t reuse_type) = 0;

    /**
     * Check if the connection has been fully established, used by connection pooling
     *
     * @return True if the connection is fully established and can be pooled
     */
    virtual bool established() = 0;

    /**
     * Tell the connection that it's in a connection pool and no longer attached to any session.
     */
    virtual void set_to_pooled() = 0;

    /**
     * Ping a backend connection
     *
     * The backend connection should perform an action that keeps the connection alive if it is currently
     * idle. The idleness of a connection is determined at the protocol level and any actions taken at the
     * protocol level should not propagate upwards.
     *
     * What this means in practice is that if a query is used to ping a backend, the result should be
     * discarded and the pinging should not interrupt ongoing queries.
     */
    virtual void ping() = 0;

    /**
     * Check if the connection can be closed in a controlled manner
     *
     * @return True if the connection can be closed without interrupting anything
     */
    virtual bool can_close() const = 0;

    virtual const BackendDCB* dcb() const = 0;
    virtual BackendDCB*       dcb() = 0;
    virtual mxs::Component*   upstream() const = 0;
};

class UserAccountCache;

/**
 * An interface which a user account manager must implement. The instance is shared between all threads.
 */
class UserAccountManager
{
public:
    virtual ~UserAccountManager() = default;

    /**
     * Start the user account manager. Should be called after creation.
     */
    virtual void start() = 0;

    /**
     * Stop the user account manager. Should be called before destruction.
     */
    virtual void stop() = 0;

    /**
     * Notify the manager that its data should be updated. The updating may happen
     * in a separate thread.
     */
    virtual void update_user_accounts() = 0;

    /**
     * Set the username and password the manager should use when accessing backends.
     *
     * @param user Username
     * @param pw Password, possibly encrypted
     */
    virtual void set_credentials(const std::string& user, const std::string& pw) = 0;

    virtual void set_backends(const std::vector<SERVER*>& backends) = 0;
    virtual void set_union_over_backends(bool union_over_backends) = 0;
    virtual void set_strip_db_esc(bool strip_db_esc) = 0;

    enum class UsersFileUsage : uint32_t
    {
        ADD_WHEN_LOAD_OK,   /* Default. Use file when normal fetch succeeds. */
        FILE_ONLY_ALWAYS,   /* Use users from file only, even when backends are down. */
    };

    /**
     * Set an additional file to read users from and when the file is read.
     * The format of the file is protocol-specific. Json is recommended.
     *
     * @param filepath Path of file. Empty string disables the feature.
     * @param file_usage When/how the file is used. Bitfield of UserAccountsFileUsage values.
     */
    virtual void set_user_accounts_file(const std::string& filepath, UsersFileUsage file_usage) = 0;

    /**
     * Which protocol this manager can be used with. Currently, it's assumed that the user data managers
     * do not have listener-specific settings. If multiple listeners with the same protocol name feed
     * the same service, only one manager is required.
     */
    virtual std::string protocol_name() const = 0;

    /**
     * Create a thread-local account cache linked to this account manager.
     *
     * @return A new user account cache
     */
    virtual std::unique_ptr<UserAccountCache> create_user_account_cache() = 0;

    /**
     * Set owning service.
     *
     * @param service
     */
    virtual void set_service(SERVICE* service) = 0;

    /**
     * Print contents to a json array.
     *
     * @return Users as json
     */
    virtual json_t* users_to_json() const = 0;

    /**
     * Get the point in time when the users were last loaded
     *
     * @return The point in time when the users were last loaded
     */
    virtual time_t last_update() const = 0;
};

class UserAccountCache
{
public:
    virtual ~UserAccountCache() = default;
    virtual void update_from_master() = 0;
};

template<class ProtocolImplementation>
class ProtocolApiGenerator
{
public:
    ProtocolApiGenerator() = delete;
    ProtocolApiGenerator(const ProtocolApiGenerator&) = delete;
    ProtocolApiGenerator& operator=(const ProtocolApiGenerator&) = delete;

    static mxs::ProtocolModule* create_protocol_module(const std::string& name, Listener* listener)
    {
        return ProtocolImplementation::create(name, listener);
    }

    static MXS_PROTOCOL_API s_api;
};

template<class ProtocolImplementation>
MXS_PROTOCOL_API ProtocolApiGenerator<ProtocolImplementation>::s_api =
{
    &ProtocolApiGenerator<ProtocolImplementation>::create_protocol_module,
};
}

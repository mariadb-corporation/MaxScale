/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/protocol.hh>
#include <maxscale/authenticator.hh>

class BackendDCB;
class ClientDCB;
class DCB;
class SERVICE;

namespace maxscale
{
class BackendConnection;
class ClientConnection;
class ConfigParameters;
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
     * Get rejection message. The protocol should return an error indicating that access to MaxScale
     * has been temporarily suspended.
     *
     * @param host The host that is blocked
     * @return A buffer containing the error message
     */
    virtual GWBUF* reject(const std::string& host)
    {
        return nullptr;
    }

    /**
     * Get protocol module name.
     *
     * @return Module name
     */
    virtual std::string name() const = 0;

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

    virtual ClientDCB*       dcb() = 0;
    virtual const ClientDCB* dcb() const = 0;

    virtual void wakeup()
    {
        // Should not be called for non-supported protocols.
        mxb_assert(!true);
    }

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
     * Reuse a connection. The connection was in the persistent pool
     * and will now be taken into use again.
     *
     * @param dcb               The connection to be reused.
     * @param upstream          The upstream component.
     *
     * @return True, if the connection can be reused, false otherwise.
     *         If @c false is returned, the @c dcb should be closed.
     */
    virtual bool reuse_connection(BackendDCB* dcb, mxs::Component* upstream) = 0;

    /**
     * Check if the connection has been fully established, used by connection pooling
     *
     * @return True if the connection is fully established and can be pooled
     */
    virtual bool established() = 0;

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
     * @return True if the connection can be closed without interruping anything
     */
    virtual bool can_close() const = 0;

    /**
     * How many seconds has the connection been idle
     *
     * @return Number of seconds the connection has been idle
     */
    virtual int64_t seconds_idle() const = 0;

    virtual const BackendDCB* dcb() const = 0;
    virtual BackendDCB*       dcb() = 0;
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

    static mxs::ProtocolModule* create_protocol_module()
    {
        // If protocols require non-authentication-related settings, add passing them here.
        // The unsolved issue is how to separate listener, protocol and authenticator-settings from
        // each other. Currently this is mostly a non-issue as the only authenticator with a setting
        // is gssapi.
        return ProtocolImplementation::create();
    }

    static MXS_PROTOCOL_API s_api;
};

template<class ProtocolImplementation>
MXS_PROTOCOL_API ProtocolApiGenerator<ProtocolImplementation>::s_api =
{
    &ProtocolApiGenerator<ProtocolImplementation>::create_protocol_module,
};
}

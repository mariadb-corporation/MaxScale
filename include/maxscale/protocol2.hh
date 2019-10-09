/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/protocol.hh>

class BackendDCB;
class ClientDCB;
class DCB;
class SERVICE;

namespace maxscale
{
class ClientConnection;
class BackendConnection;
class UserAccountManager;

class ProtocolModule
{
public:
    enum Capabilities
    {
        CAP_AUTHDATA = (1u << 0)        // The protocol implements an authentication data manager
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
     * Load users for all authenticators.
     *
     * @param service The service to load from
     * @return MXS_AUTH_LOADUSERS_OK on success
     */
    virtual int load_auth_users(SERVICE* service) = 0;

    /**
     * Print a list of authenticator users to DCB.
     *
     * @param output Output DCB
     */
    virtual void print_auth_users(DCB* output) = 0;

    /**
     * Print a list of authenticator users to json.
     *
     * @return JSON user list
     */
    virtual json_t* print_auth_users_json() = 0;

    virtual std::unique_ptr<UserAccountManager> create_user_data_manager()
    {
        return nullptr;
    }

    virtual uint64_t capabilities() const
    {
        return 0;
    }
};

/**
 * Client protocol connection interface. All protocols must implement this.
 */
class ClientConnection : public ProtocolConnection
{
public:
    enum Capabilities
    {
        CAP_BACKEND = (1 << 0)      // The protocol supports backend communication
    };

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

    virtual int64_t capabilities() const
    {
        return 0;
    }

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
};

/**
 * Partial client protocol implementation. More fields and functions may be added later.
 */
class ClientConnectionBase : public ClientConnection
{
public:
    json_t*          diagnostics_json() const override;
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

    virtual const BackendDCB* dcb() const = 0;
    virtual BackendDCB*       dcb() = 0;
};

/**
 * An interface which a user account manager class must implement. So far, this is just
 * a draft and more features will be added as clarity increases.
 */
class UserAccountManager
{
public:
    /**
     * Check if user@host exists and can access the requested database. Does not check password or
     * any other authentication credentials.
     *
     * @param user Client username
     * @param host Client hostname
     * @param requested_db Database requested by client. May be empty.
     * @return True if user account is valid
     */
    virtual bool
    check_user(const std::string& user, const std::string& host, const std::string& requested_db) = 0;

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

    /**
     * Which protocol this manager can be used with. Currently, it's assumed that the user data managers
     * do not have listener-specific settings. If multiple listeners with the same protocol name feed
     * the same service, only one manager is required.
     */
    virtual std::string protocol_name() const = 0;
};

template<class ProtocolImplementation>
class ProtocolApiGenerator
{
public:
    ProtocolApiGenerator() = delete;
    ProtocolApiGenerator(const ProtocolApiGenerator&) = delete;
    ProtocolApiGenerator& operator=(const ProtocolApiGenerator&) = delete;

    static mxs::ProtocolModule* create_protocol_module(const std::string& auth_name,
                                                       const std::string& auth_opts)
    {
        return ProtocolImplementation::create(auth_name, auth_opts);
    }

    static MXS_PROTOCOL_API s_api;
};

template<class ProtocolImplementation>
MXS_PROTOCOL_API ProtocolApiGenerator<ProtocolImplementation>::s_api =
{
    &ProtocolApiGenerator<ProtocolImplementation>::create_protocol_module,
};
}

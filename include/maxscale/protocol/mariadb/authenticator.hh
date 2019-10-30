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

#include <maxscale/authenticator.hh>
#include <maxscale/dcb.hh>

/** Return values for extract and authenticate entry points */
#define MXS_AUTH_SUCCEEDED             0/**< Authentication was successful */
#define MXS_AUTH_FAILED                1/**< Authentication failed */
#define MXS_AUTH_FAILED_DB             2/**< Authentication failed, database not found */
#define MXS_AUTH_FAILED_SSL            3/**< SSL authentication failed */
#define MXS_AUTH_INCOMPLETE            4/**< Authentication is not yet complete */
#define MXS_AUTH_SSL_INCOMPLETE        5/**< SSL connection is not yet complete */
#define MXS_AUTH_SSL_COMPLETE          6/**< SSL connection complete or not required */
#define MXS_AUTH_NO_SESSION            7
#define MXS_AUTH_BAD_HANDSHAKE         8/**< Malformed client packet */
#define MXS_AUTH_FAILED_WRONG_PASSWORD 9/**< Client provided wrong password */

/**
 * Authentication states
 *
 * The state usually goes from INIT to CONNECTED and alternates between
 * MESSAGE_READ and RESPONSE_SENT until ending up in either FAILED or COMPLETE.
 *
 * If the server immediately rejects the connection, the state ends up in
 * HANDSHAKE_FAILED. If the connection creation would block, instead of going to
 * the CONNECTED state, the connection will be in PENDING_CONNECT state until
 * the connection can be created.
 */
enum mxs_auth_state_t
{
    MXS_AUTH_STATE_INIT,            /**< Initial authentication state */
    MXS_AUTH_STATE_PENDING_CONNECT, /**< Connection creation is underway */
    MXS_AUTH_STATE_CONNECTED,       /**< Network connection to server created */
    MXS_AUTH_STATE_MESSAGE_READ,    /**< Read a authentication message from the server */
    MXS_AUTH_STATE_RESPONSE_SENT,   /**< Responded to the read authentication message */
    MXS_AUTH_STATE_FAILED,          /**< Authentication failed */
    MXS_AUTH_STATE_HANDSHAKE_FAILED,/**< Authentication failed immediately */
    MXS_AUTH_STATE_COMPLETE         /**< Authentication is complete */
};

namespace maxscale
{

class ClientAuthenticator;
class BackendAuthenticator;

/**
 * The base class of all authenticators. Contains the global data for an authenticator module instance.
 */
class AuthenticatorModule : public AuthenticatorModuleBase
{
public:
    AuthenticatorModule(const AuthenticatorModule&) = delete;
    AuthenticatorModule& operator=(const AuthenticatorModule&) = delete;

    enum Capabilities
    {
         CAP_REAUTHENTICATE = (1 << 1),  /**< Does the module support reauthentication? */
         CAP_BACKEND_AUTH = (1 << 2),    /**< Does the module support backend authentication? */
         CAP_CONC_LOAD_USERS = (1 << 3)  /**< Does the module support concurrent user loading? */
    };

    AuthenticatorModule() = default;
    virtual ~AuthenticatorModule() = default;

    /**
     * Create a client authenticator.
     *
     * @return Client authenticator
     */
    virtual std::unique_ptr<ClientAuthenticator> create_client_authenticator() = 0;

    /**
     * Create a new backend authenticator. Should only be implemented by authenticator modules which
     * also support backend authentication.
     *
     * @return Backend authenticator
     */
    virtual std::unique_ptr<BackendAuthenticator> create_backend_authenticator() = 0;

    // Load or update authenticator user data
    virtual int load_users(SERVICE* service) = 0;

    // Print diagnostic output to a DCB.
    virtual void diagnostics(DCB* output) = 0;

    /**
     * @brief Return diagnostic information about the authenticator
     *
     * The authenticator module should return information about its internal
     * state when this function is called.
     *
     * @return JSON representation of the authenticator
     */
    virtual json_t* diagnostics_json() = 0;

    /**
     * Get module runtime capabilities. Returns 0 by default.
     *
     * @return Capabilities as a bitfield
     */
    virtual uint64_t capabilities() const;
};

/**
 * The base class of authenticator client sessions. Contains session-specific data for an authenticator.
 */
class ClientAuthenticator
{
public:
    using ByteVec = std::vector<uint8_t>;

    ClientAuthenticator(const ClientAuthenticator&) = delete;
    ClientAuthenticator& operator=(const ClientAuthenticator&) = delete;

    ClientAuthenticator() = default;
    virtual ~ClientAuthenticator() = default;

    /**
     * Get module runtime capabilities.
     *
     * @return Capabilities as a bitfield
     */
    virtual uint64_t capabilities() const = 0;

    /**
     * Extract client from a buffer and place it in a structure shared at the session level.
     * Typically, this is called just before the authenticate-entrypoint.
     *
     * @param client Client dcb
     * @param buffer Packet from client
     * @return True on success
     */
    virtual bool extract(DCB* client, GWBUF* buffer) = 0;

    // Determine whether the connection can support SSL.
    virtual bool ssl_capable(DCB* client) = 0;

    // Carry out the authentication.
    virtual int authenticate(DCB* client) = 0;

    /**
     * This entry point was added to avoid calling authenticator functions
     * directly when a COM_CHANGE_USER command is executed. Not implemented by most authenticators.
     *
     * @param dcb The connection
     * @param scramble Scramble sent by MaxScale to client
     * @param scramble_len Scramble length
     * @param auth_token Authentication token sent by client
     * @param output Hashed client password used by backend protocols
     * @return 0 on success
     */
    virtual int reauthenticate(DCB* client, uint8_t* scramble, size_t scramble_len,
                               const ByteVec& auth_token, uint8_t* output);
};

// Helper template which stores the module reference.
template <class AuthModule>
class ClientAuthenticatorT : public ClientAuthenticator
{
public:
    /**
     * Constructor.
     *
     * @param module The global module data
     */
    ClientAuthenticatorT(AuthModule* module)
    : m_module(*module)
    {
    }

    uint64_t capabilities() const override
    {
        return m_module.capabilities();
    }

protected:
    AuthModule& m_module;
};

/**
 * The base class for all authenticator backend sessions. Created by the client session.
 */
class BackendAuthenticator
{
public:
    BackendAuthenticator(const BackendAuthenticator&) = delete;
    BackendAuthenticator& operator=(const BackendAuthenticator&) = delete;

    BackendAuthenticator() = default;
    virtual ~BackendAuthenticator() = default;

    // Extract backend data from a buffer. Typically, this is called just before the authenticate-entrypoint.
    virtual bool extract(DCB* client, GWBUF* buffer) = 0;

    // Determine whether the connection can support SSL.
    virtual bool ssl_capable(DCB* client) = 0;

    // Carry out the authentication.
    virtual int authenticate(DCB* client) = 0;
};

const char* to_string(mxs_auth_state_t state);

}

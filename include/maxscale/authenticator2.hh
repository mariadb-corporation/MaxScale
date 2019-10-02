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

namespace maxscale
{

class ClientAuthenticator;
class BackendAuthenticator;

/**
 * The base class of all authenticators. Contains the global data for an authenticator module instance.
 */
class AuthenticatorModule
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

    // Create a client session.
    virtual std::unique_ptr<ClientAuthenticator> create_client_authenticator() = 0;

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

    /**
     * Get name of supported protocol module.
     *
     * @return Supported protocol
     */
    virtual std::string supported_protocol() const = 0;
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

    /**
     * Create a new backend authenticator linked to the client authenticator. Should only be implemented by
     * authenticator modules which also support backend authentication.
     *
     * @return Backend authenticator
     */
    virtual std::unique_ptr<BackendAuthenticator> create_backend_authenticator() = 0;
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

template<class AuthenticatorImplementation>
class AuthenticatorApiGenerator
{
public:
    AuthenticatorApiGenerator() = delete;
    AuthenticatorApiGenerator(const AuthenticatorApiGenerator&) = delete;
    AuthenticatorApiGenerator& operator=(const AuthenticatorApiGenerator&) = delete;

    static AuthenticatorModule* createInstance(char** options)
    {
        AuthenticatorModule* instance = nullptr;
        MXS_EXCEPTION_GUARD(instance = AuthenticatorImplementation::create(options));
        return instance;
    }

    static AUTHENTICATOR_API s_api;
};

template<class AuthenticatorImplementation>
AUTHENTICATOR_API AuthenticatorApiGenerator<AuthenticatorImplementation>::s_api =
{
    &AuthenticatorApiGenerator<AuthenticatorImplementation>::createInstance
};

}

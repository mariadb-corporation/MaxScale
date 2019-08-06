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
#include <maxscale/listener.hh>

namespace maxscale
{

class AuthenticatorSession;
class AuthenticatorBackendSession;

/**
 * The base class of all authenticators. Contains the global data for an authenticator module instance.
 */
class Authenticator
{
public:
    enum Capabilities
    {
         CAP_REAUTHENTICATE = (1 << 1),  /**< Does the module support reauthentication? */
         CAP_BACKEND_AUTH = (1 << 2),    /**< Does the module support backend authentication? */
         CAP_CONC_LOAD_USERS = (1 << 3)  /**< Does the module support concurrent user loading? */
    };

    virtual ~Authenticator() = default;

    // Create a data structure unique to this DCB, stored in `dcb->authenticator_data`. If a module
    // does not implement this entry point, `dcb->authenticator_data` will be set to NULL.
    virtual AuthenticatorSession* createSession() = 0;

    // Load or update authenticator user data
    virtual int load_users(Listener* listener) = 0;

    // Print diagnostic output to a DCB.
    virtual void diagnostics(DCB* output, Listener* listener) = 0;

    /**
     * @brief Return diagnostic information about the authenticator
     *
     * The authenticator module should return information about its internal
     * state when this function is called.
     *
     * @params Listener object
     * @return JSON representation of the listener
     * @see jansson.h
     */
    virtual json_t* diagnostics_json(const Listener* listener) = 0;

    /**
     * Get module runtime capabilities. Returns 0 by default.
     *
     * @return Capabilities as a bitfield
     */
    virtual uint64_t capabilities() const;
};

/**
 * The base class of all authenticator sessions. Contains session-specific data for an authenticator.
 */
class AuthenticatorSession
{
public:
    virtual ~AuthenticatorSession() = default;

    // Extract client or backend data from a buffer and place it in a structure shared at the session
    // level, stored in `dcb->data`. Typically, this is called just before the authenticate-entrypoint.
    virtual bool extract(DCB* client, GWBUF* buffer) = 0;

    // Determine whether the connection can support SSL.
    virtual bool ssl_capable(DCB* client) = 0;

    // Carry out the authentication.
    virtual int authenticate(DCB* client) = 0;

    // Free extracted data. This is only called for the client side authenticators so backend
    // authenticators should not implement it.
    virtual void free_data(DCB* client) = 0;

    /**
     * This entry point was added to avoid calling authenticator functions
     * directly when a COM_CHANGE_USER command is executed. Not implemented by most authenticators.
     *
     * @param dcb The connection
     * @param user Username
     * @param token Client auth token
     * @param token_len Auth token length
     * @param scramble Scramble sent by MaxScale to client
     * @param scramble_len Scramble length
     * @param output Hashed client password used by backend protocols
     * @param output_len Hash length
     * @return 0 on success
     */
    virtual int reauthenticate(DCB* client, const char* user, uint8_t* token, size_t token_len,
                               uint8_t* scramble, size_t scramble_len,
                               uint8_t* output, size_t output_len);

    /**
     * Create a new backend session linked to the client session. Should only be implemented by
     * authenticators which also support backend authentication.
     *
     * @return Backend session
     */
    virtual AuthenticatorBackendSession* newBackendSession();

};

/**
 * Helper template which builds the authenticator c-api from the basic authenticator-classes. Should not
 * be needed once refactoring is complete.
 */
template<class AuthImplementation>
class AuthenticatorApi
{
public:
    AuthenticatorApi() = delete;
    AuthenticatorApi(const AuthenticatorApi&) = delete;
    AuthenticatorApi& operator=(const AuthenticatorApi&) = delete;

    static void* createInstance(char** options)
    {
        Authenticator* instance = nullptr;
        MXS_EXCEPTION_GUARD(instance = AuthImplementation::create(options));
        return instance;
    }

    static void* createSession(void* instance)
    {
        auto inst = static_cast<Authenticator*>(instance);
        AuthenticatorSession* session = nullptr;
        MXS_EXCEPTION_GUARD(session = inst->createSession());
        return session;
    }

    static MXS_AUTHENTICATOR s_api;
};

template<class AuthImplementation>
MXS_AUTHENTICATOR AuthenticatorApi<AuthImplementation>::s_api =
{
        &AuthenticatorApi<AuthImplementation>::createInstance,
        &AuthenticatorApi<AuthImplementation>::createSession,
};

/**
 * The base class for all authenticator backend sessions. Ideally, these should be created by the
 * authenticator client sessions. For now they must be a separate class and API struct.
 */
class AuthenticatorBackendSession : public mxs::AuthenticatorSession
{
public:
    void free_data(DCB* client) final;
};

/**
 * Another helper template for backend authenticators.
 */
template<class AuthImplementation>
class BackendAuthenticatorApi
{
public:
    BackendAuthenticatorApi() = delete;
    BackendAuthenticatorApi(const BackendAuthenticatorApi&) = delete;
    BackendAuthenticatorApi& operator=(const BackendAuthenticatorApi&) = delete;

    static void* newSession(void* instance)
    {
        AuthenticatorBackendSession* ses = nullptr;
        MXS_EXCEPTION_GUARD(ses = AuthImplementation::newSession());
        return ses;
    }

    static MXS_AUTHENTICATOR s_api;
};

template<class AuthImplementation>
MXS_AUTHENTICATOR BackendAuthenticatorApi<AuthImplementation>::s_api =
{
        nullptr,
        &BackendAuthenticatorApi<AuthImplementation>::newSession
};

}

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

/**
 * The base class of all authenticators. Contains the global data for an authenticator module instance.
 */
class Authenticator
{
public:
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

    // Reauthenticate a user. Not implemented by most authenticators.
    virtual int reauthenticate(DCB* client, const char* user, uint8_t* token, size_t token_len,
                               uint8_t* scramble, size_t scramble_len,
                               uint8_t* output, size_t output_len);
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

    static int reauthenticate(DCB* client, const char* user, uint8_t* token, size_t token_len,
                              uint8_t* scramble, size_t scramble_len, uint8_t* output, size_t output_len)
    {
        auto session = client->m_authenticator_data;
        int rval = MXS_AUTH_SSL_COMPLETE;
        MXS_EXCEPTION_GUARD(rval = session->reauthenticate(client, user, token, token_len,
                                                           scramble, scramble_len, output, output_len));
        return rval;
    }

    static MXS_AUTHENTICATOR s_api;
};

template<class AuthImplementation>
MXS_AUTHENTICATOR AuthenticatorApi<AuthImplementation>::s_api =
{
        &AuthenticatorApi<AuthImplementation>::createInstance,
        &AuthenticatorApi<AuthImplementation>::createSession,
        &AuthenticatorApi<AuthImplementation>::reauthenticate
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
        &BackendAuthenticatorApi<AuthImplementation>::newSession,
        nullptr
};

}

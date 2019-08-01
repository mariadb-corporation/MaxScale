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
    virtual AuthenticatorSession* createSession() = 0;
    virtual int load_users(Listener* listener) = 0;
    virtual void diagnostics(DCB* output, Listener* listener) = 0;
    virtual json_t* diagnostics_json(const Listener* listener) = 0;
};

/**
 * The base class of all authenticator sessions. Contains session-specific data for an authenticator.
 */
class AuthenticatorSession
{
public:
    virtual ~AuthenticatorSession() = default;
    virtual bool extract(DCB* client, GWBUF* buffer) = 0;
    virtual bool ssl_capable(DCB* client) = 0;
    virtual int authenticate(DCB* client) = 0;
    virtual void free_data(DCB* client) = 0;
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

    static bool extractData(DCB* client, GWBUF* buffer)
    {
        auto session = static_cast<AuthenticatorSession*>(client->m_authenticator_data);
        bool success = false;
        MXS_EXCEPTION_GUARD(success = session->extract(client, buffer));
        return success;
    }

    static bool sslCapable(DCB* client)
    {
        auto session = static_cast<AuthenticatorSession*>(client->m_authenticator_data);
        bool ssl = false;
        MXS_EXCEPTION_GUARD(ssl = session->ssl_capable(client));
        return ssl;
    }

    static int authenticate(DCB* client)
    {
        auto session = static_cast<AuthenticatorSession*>(client->m_authenticator_data);
        int rval = MXS_AUTH_SSL_COMPLETE;
        MXS_EXCEPTION_GUARD(rval = session->authenticate(client));
        return rval;
    }

    static void freeData(DCB* client)
    {
        auto session = static_cast<AuthenticatorSession*>(client->m_authenticator_data);
        MXS_EXCEPTION_GUARD(session->free_data(client));
    }

    static void destroySession(void* session)
    {
        auto ses = static_cast<AuthenticatorSession*>(session);
        MXS_EXCEPTION_GUARD(delete ses);
    }

    static int loadUsers(Listener* listener)
    {
        auto auth = static_cast<Authenticator*>(listener->auth_instance());
        int rval = MXS_AUTH_LOADUSERS_ERROR;
        MXS_EXCEPTION_GUARD(rval = auth->load_users(listener));
        return rval;
    }

    static void diagnostics(DCB* output, Listener* listener)
    {
        auto auth = static_cast<Authenticator*>(listener->auth_instance());
        MXS_EXCEPTION_GUARD(auth->diagnostics(output, listener));
    }

    static json_t* diagnostics_json(const Listener* listener)
    {
        auto auth = static_cast<Authenticator*>(listener->auth_instance());
        json_t* rval = nullptr;
        MXS_EXCEPTION_GUARD(rval = auth->diagnostics_json(nullptr));
        return rval;
    }

    static int reauthenticate(DCB* client, const char* user, uint8_t* token, size_t token_len,
                              uint8_t* scramble, size_t scramble_len, uint8_t* output, size_t output_len)
    {
        auto session = static_cast<AuthenticatorSession*>(client->m_authenticator_data);
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
        &AuthenticatorApi<AuthImplementation>::extractData,
        &AuthenticatorApi<AuthImplementation>::sslCapable,
        &AuthenticatorApi<AuthImplementation>::authenticate,
        &AuthenticatorApi<AuthImplementation>::freeData,
        &AuthenticatorApi<AuthImplementation>::destroySession,
        &AuthenticatorApi<AuthImplementation>::loadUsers,
        &AuthenticatorApi<AuthImplementation>::diagnostics,
        &AuthenticatorApi<AuthImplementation>::diagnostics_json,
        &AuthenticatorApi<AuthImplementation>::reauthenticate
};

/**
 * The base class for all authenticator backend sessions. Ideally, these should be created by the
 * authenticator client sessions. For now they must be a separate class and API struct.
 */
class AuthenticatorBackendSession
{
public:

    virtual ~AuthenticatorBackendSession() = default;
    virtual bool extract(DCB* backend, GWBUF* buffer) = 0;
    virtual bool ssl_capable(DCB* backend) = 0;
    virtual int authenticate(DCB* backend) = 0;
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

    static bool extractData(DCB* backend, GWBUF* buffer)
    {
        auto session = static_cast<AuthenticatorBackendSession*>(backend->m_authenticator_data);
        bool success = false;
        MXS_EXCEPTION_GUARD(success = session->extract(backend, buffer));
        return success;
    }

    static bool sslCapable(DCB* backend)
    {
        auto session = static_cast<AuthenticatorBackendSession*>(backend->m_authenticator_data);
        bool ssl = false;
        MXS_EXCEPTION_GUARD(ssl = session->ssl_capable(backend));
        return ssl;
    }

    static int authenticate(DCB* backend)
    {
        auto session = static_cast<AuthenticatorBackendSession*>(backend->m_authenticator_data);
        int rval = MXS_AUTH_SSL_COMPLETE;
        MXS_EXCEPTION_GUARD(rval = session->authenticate(backend));
        return rval;
    }

    static void freeSession(void* session)
    {
        auto ses = static_cast<AuthenticatorBackendSession*>(session);
        MXS_EXCEPTION_GUARD(delete ses);
    }

    static MXS_AUTHENTICATOR s_api;
};

template<class AuthImplementation>
MXS_AUTHENTICATOR BackendAuthenticatorApi<AuthImplementation>::s_api =
{
        nullptr,
        &BackendAuthenticatorApi<AuthImplementation>::newSession,
        &BackendAuthenticatorApi<AuthImplementation>::extractData,
        &BackendAuthenticatorApi<AuthImplementation>::sslCapable,
        &BackendAuthenticatorApi<AuthImplementation>::authenticate,
        nullptr,
        &BackendAuthenticatorApi<AuthImplementation>::freeSession,
        nullptr,
        nullptr,
        nullptr,
        nullptr
};

}

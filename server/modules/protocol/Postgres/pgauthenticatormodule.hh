/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "postgresprotocol.hh"
#include <maxscale/authenticator.hh>
#include <optional>

class PgAuthenticatorModule;

enum class UserEntryType
{
    UNKNOWN,
    NO_HBA_ENTRY,
    NO_AUTH_ID_ENTRY,
    METHOD_NOT_SUPPORTED,
    USER_ACCOUNT_OK,
};

struct AuthIdEntry
{
    std::string name;
    std::string password;
    bool        super {false};
    bool        inherit {false};
    bool        can_login {false};

    bool operator==(const AuthIdEntry& rhs) const;
};

struct UserEntryResult
{
    UserEntryType type {UserEntryType::UNKNOWN};
    uint32_t      line_no {0};
    std::string   auth_method;
    AuthIdEntry   authid_entry;
};

struct AuthenticationData
{
    std::string     user;           /**< Username */
    UserEntryResult user_entry;     /**< User account information */

    std::vector<uint8_t>   client_token;    /**< Token sent by client */
    PgAuthenticatorModule* auth_module {nullptr};
};

class PgProtocolData;

/**
 * The base class of authenticator client sessions. Contains session-specific data for an authenticator.
 */
class PgClientAuthenticator
{
public:
    PgClientAuthenticator(const PgClientAuthenticator&) = delete;
    PgClientAuthenticator& operator=(const PgClientAuthenticator&) = delete;

    PgClientAuthenticator() = default;
    virtual ~PgClientAuthenticator() = default;


    virtual GWBUF authentication_request() = 0;

    struct ExchRes
    {
        enum class Status
        {
            READY,      /**< Exchange with client complete, should continue to password check */
            INCOMPLETE, /**< In progress, call again once client responds */
            FAIL,       /**< Packet processing failed */
        };

        Status status {Status::FAIL};   /**< Authentication exchange status */
        GWBUF  packet;                  /**< Packet to send to client */
    };

    /**
     * Communicate with client. Just return a buffer for now. If/once more complicated methods are added,
     * the function parameters will more closely resemble MariaDB-authenticators.
     *
     * @param input Client buffer
     * @return Result structure
     */
    virtual ExchRes exchange(GWBUF&& input, PgProtocolData& session) = 0;

    struct AuthRes
    {
        enum class Status
        {
            FAIL,           /**< Authentication failed */
            FAIL_WRONG_PW,  /**< Client provided wrong password */
            SUCCESS,        /**< Authentication was successful */
        };

        Status      status {Status::FAIL};
        std::string msg;
    };

    /**
     * Check client token against the password.
     *
     * @param data Protocol session data
     */
    virtual AuthRes authenticate(PgProtocolData& data) = 0;
};

/**
 * The base class for all backend authenticator modules for MariaDB-protocol.
 */
class PgBackendAuthenticator
{
public:
    PgBackendAuthenticator(const PgBackendAuthenticator&) = delete;
    PgBackendAuthenticator& operator=(const PgBackendAuthenticator&) = delete;
    PgBackendAuthenticator() = default;
    virtual ~PgBackendAuthenticator() = default;

    /**
     * Exchange authentication packets. Reads the input and generate output to send to server.
     *
     * @param input Packet from backend
     * @param session Protocol session
     * @return Reply to backend. An empty optional on error. An empty yet existing GWBUF means the
     * operation succeeded but there is nothing to send.
     */
    virtual std::optional<GWBUF> exchange(GWBUF&& input, PgProtocolData& session) = 0;
};

class PgAuthenticatorModule : public mxs::AuthenticatorModule
{
public:
    virtual ~PgAuthenticatorModule() = default;

    virtual std::unique_ptr<PgClientAuthenticator>  create_client_authenticator() const = 0;
    virtual std::unique_ptr<PgBackendAuthenticator> create_backend_authenticator() const = 0;

    std::string supported_protocol() const override;
};

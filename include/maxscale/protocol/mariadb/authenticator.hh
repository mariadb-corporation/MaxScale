/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/authenticator.hh>
#include <maxscale/protocol/mariadb/common_constants.hh>
#include <unordered_set>

class DCB;
class SERVICE;
class GWBUF;
class MYSQL_session;

namespace maxscale
{
class Buffer;
}

namespace mariadb
{

class ClientAuthenticator;
class BackendAuthenticator;
struct BackendAuthData;

using SClientAuth = std::unique_ptr<ClientAuthenticator>;
using SBackendAuth = std::unique_ptr<BackendAuthenticator>;

struct UserEntry
{
    std::string username;       /**< Username */
    std::string host_pattern;   /**< Hostname or IP, may have wildcards */
    std::string plugin;         /**< Auth plugin to use */
    std::string password;       /**< Auth data used by native auth plugin */
    std::string auth_string;    /**< Auth data used by other plugins */

    bool ssl {false};           /**< Should the user connect with ssl? */
    bool super_priv {false};    /**< Does the user have superuser privilege? */
    bool global_db_priv {false};/**< Does the user have access to all databases? */
    bool proxy_priv {false};    /**< Does the user have proxy grants? */

    bool        is_role {false};/**< Is the user a role? */
    std::string default_role;   /**< Default role if any */

    bool        operator==(const UserEntry& rhs) const;
    static bool host_pattern_is_more_specific(const UserEntry& lhs, const UserEntry& rhs);
};

// User account search result descriptor
enum class UserEntryType
{
    USER_NOT_FOUND,
    ROOT_ACCESS_DENIED,
    ANON_PROXY_ACCESS_DENIED,
    DB_ACCESS_DENIED,
    BAD_DB,
    PLUGIN_IS_NOT_LOADED,
    USER_ACCOUNT_OK,
};

struct UserEntryResult
{
    mariadb::UserEntry entry;
    UserEntryType      type {UserEntryType::USER_NOT_FOUND};
};

/**
 * The base class of all authenticators for MariaDB-protocol. Contains the global data for
 * an authenticator module instance.
 */
class AuthenticatorModule : public mxs::AuthenticatorModule
{
public:
    AuthenticatorModule(const AuthenticatorModule&) = delete;
    AuthenticatorModule& operator=(const AuthenticatorModule&) = delete;

    enum Capabilities
    {
        CAP_ANON_USER = (1 << 0),   /**< Does the module allow anonymous users? */
    };

    AuthenticatorModule() = default;
    virtual ~AuthenticatorModule() = default;

    /**
     * Create a client authenticator.
     *
     * @return Client authenticator
     */
    virtual SClientAuth create_client_authenticator() = 0;

    /**
     * Create a new backend authenticator.
     *
     * @param auth_data Data shared with backend connection
     * @return Backend authenticator
     */
    virtual SBackendAuth create_backend_authenticator(BackendAuthData& auth_data) = 0;

    /**
     * List the server authentication plugins this authenticator module supports.
     *
     * @return Supported authenticator plugins
     */
    virtual const std::unordered_set<std::string>& supported_plugins() const = 0;

    /**
     * Get module runtime capabilities. Returns 0 by default.
     *
     * @return Capabilities as a bitfield
     */
    virtual uint64_t capabilities() const;
};

using SAuthModule = std::unique_ptr<AuthenticatorModule>;

/**
 * The base class of authenticator client sessions. Contains session-specific data for an authenticator.
 */
class ClientAuthenticator
{
public:
    using ByteVec = std::vector<uint8_t>;

    enum class ExchRes
    {
        FAIL,           /**< Packet processing failed */
        INCOMPLETE,     /**< Should be called again after client responds to output */
        READY           /**< Exchange with client complete, should continue to password check */
    };

    // Return values for authenticate()-function
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

    ClientAuthenticator(const ClientAuthenticator&) = delete;
    ClientAuthenticator& operator=(const ClientAuthenticator&) = delete;

    ClientAuthenticator() = default;
    virtual ~ClientAuthenticator() = default;

    /**
     * Exchange authentication packets. The module should read the input, optionally write to output,
     * and return status.
     *
     * @param input Packet from client
     * @param ses MySQL session
     * @param output Output for a packet that is sent to the client
     * @return Authentication status
     */
    virtual ExchRes exchange(GWBUF* input, MYSQL_session* ses, mxs::Buffer* output) = 0;

    /**
     * Check client token against the password.
     *
     * @param entry User account entry
     * @param session Protocol session data
     */
    virtual AuthRes authenticate(const UserEntry* entry, MYSQL_session* session) = 0;
};

/**
 * Data shared between the backend connection and its authenticator module.
 */
struct BackendAuthData
{
    BackendAuthData(const char* srv_name);

    const char* const    servername;    /**< Server name, used for logging */
    const MYSQL_session* client_data;   /**< Protocol-session data */

    uint8_t scramble[MYSQL_SCRAMBLE_LEN] {0};   /**< Server scramble, received from backend */
};

/**
 * The base class for all backend authenticator modules for MariaDB-protocol.
 */
class BackendAuthenticator
{
public:
    // Return values for authenticate-functions. TODO: change to bool if no more values needed
    enum class AuthRes
    {
        SUCCESS,    /**< Authentication was successful */
        FAIL,       /**< Authentication failed */
    };

    BackendAuthenticator(const BackendAuthenticator&) = delete;
    BackendAuthenticator& operator=(const BackendAuthenticator&) = delete;

    BackendAuthenticator() = default;
    virtual ~BackendAuthenticator() = default;

    /**
     * Exchange authentication packets. The plugin should read the input, optionally write to output,
     * and return status.
     *
     * @param input Packet from backend
     * @param output Output for a packet that will be sent to backend
     * @return Authentication status
     */
    virtual AuthRes exchange(const mxs::Buffer& input, mxs::Buffer* output) = 0;

protected:
    // Common error message formats, used in several authenticators.
    static constexpr const char* WRONG_PLUGIN_REQ =
        "'%s' asked for authentication plugin '%s' when authenticating '%s'. Only '%s' is supported.";
    static constexpr const char* MALFORMED_AUTH_SWITCH =
        "Received malformed AuthSwitchRequest-packet from '%s'.";
};
}

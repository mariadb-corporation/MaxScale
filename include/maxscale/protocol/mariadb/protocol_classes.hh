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
#include <maxscale/authenticator2.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/session.hh>
#include <maxscale/protocol/mariadb/common_constants.hh>

class GWBUF;

/*
 * Data shared with authenticators
 */
class MYSQL_session : public MXS_SESSION::ProtocolData
{
public:

    /**
     * Contains client capabilities. The client sends this data in the handshake response-packet, and the
     * same data is sent to backends. Usually only the client protocol should write to these.
     */
    class ClientInfo
    {
    public:
        uint32_t m_client_capabilities {0};     /*< Basic client capabilities */
        uint32_t m_extra_capabilities {0};      /*< MariaDB 10.2 capabilities */

        /**
         * Connection character set (default latin1 ). Usually just one byte is needed. COM_CHANGE_USER
         * sends two. */
        uint16_t m_charset {0x8};
    };

    bool     ssl_capable() const;
    uint32_t client_capabilities() const;
    uint32_t extra_capabilitites() const;

    uint8_t     client_sha1[MYSQL_SCRAMBLE_LEN] {0};/*< SHA1(password) */
    std::string user;                               /*< username       */
    std::string db;                                 /*< database       */
    uint8_t     next_sequence {0};                  /*< Next packet sequence */
    bool        changing_user {false};              /*< True if a COM_CHANGE_USER is in progress */

    ClientInfo client_info;     /**< Client capabilities from handshake response packet */

    // Authentication token storage. Used by different authenticators.
    mxs::ClientAuthenticator::ByteVec auth_token;
};

class MySQLProtocolModule : public mxs::ProtocolModule
{
public:
    static MySQLProtocolModule* create(const std::string& auth_name, const std::string& auth_opts);

    std::unique_ptr<mxs::ClientConnection>
    create_client_protocol(MXS_SESSION* session, mxs::Component* component) override;

    std::unique_ptr<mxs::BackendConnection>
    create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component) override;

    std::string auth_default() const override;
    GWBUF*      reject(const std::string& host) override;

    uint64_t capabilities() const override;

    std::string name() const override;

    int  load_auth_users(SERVICE* service) override;
    void print_auth_users(DCB* output) override;

    json_t* print_auth_users_json() override;

    std::unique_ptr<mxs::UserAccountManager>
    create_user_data_manager(const std::string& service_name) override;

private:
    std::unique_ptr<mxs::AuthenticatorModule> m_auth_module;
};

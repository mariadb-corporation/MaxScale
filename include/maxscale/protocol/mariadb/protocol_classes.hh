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
#include <maxscale/session.hh>
#include <maxscale/protocol/mariadb/common_constants.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>

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
    std::string remote;                             /*< client ip      */
    std::string db;                                 /*< database       */
    uint8_t     next_sequence {0};                  /*< Next packet sequence */
    bool        changing_user {false};              /*< True if a COM_CHANGE_USER is in progress */

    ClientInfo client_info;     /**< Client capabilities from handshake response packet */

    // Authentication token storage. Used by different authenticators.
    mariadb::ClientAuthenticator::ByteVec auth_token;

    // Authenticator modules the session may use. TODO: Change to reference once BLR is cleaned up.
    const std::vector<mariadb::SAuthModule>* allowed_authenticators {nullptr};

    // Authenticator module currently in use by the session. May change on COM_CHANGE_USER.
    mariadb::AuthenticatorModule* m_current_authenticator {nullptr};
};

#pragma once

/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/protocol2.hh>
#include <openssl/sha.h>

#define CDC_SMALL_BUFFER       1024
#define CDC_METHOD_MAXLEN      128
#define CDC_USER_MAXLEN        128
#define CDC_HOSTNAME_MAXLEN    512
#define CDC_USERAGENT_MAXLEN   1024
#define CDC_FIELD_MAXLEN       8192
#define CDC_REQUESTLINE_MAXLEN 8192

#define CDC_UNDEFINED             0
#define CDC_ALLOC                 1
#define CDC_STATE_WAIT_FOR_AUTH   2
#define CDC_STATE_AUTH_OK         3
#define CDC_STATE_AUTH_FAILED     4
#define CDC_STATE_AUTH_ERR        5
#define CDC_STATE_AUTH_NO_SESSION 6
#define CDC_STATE_REGISTRATION    7
#define CDC_STATE_HANDLE_REQUEST  8
#define CDC_STATE_CLOSE           9

#define CDC_UUID_LEN 32
#define CDC_TYPE_LEN 16

class CDCAuthenticatorModule;

class CDCClientAuthenticator
{
public:
    CDCClientAuthenticator(CDCAuthenticatorModule& module)
        : m_module(module)
    {
    }

    ~CDCClientAuthenticator() = default;
    bool extract(DCB* client, GWBUF* buffer);

    bool ssl_capable(DCB* client)
    {
        return false;
    }

    int authenticate(DCB* client);

private:
    bool set_client_data(uint8_t* client_auth_packet, int client_auth_packet_size);

    char    m_user[CDC_USER_MAXLEN + 1] {'\0'}; /*< username for authentication */
    uint8_t m_auth_data[SHA_DIGEST_LENGTH] {0}; /*< Password Hash               */

    CDCAuthenticatorModule& m_module;
};

/**
 * CDC protocol
 */
class CDCClientConnection : public mxs::ClientConnectionBase
{
public:
    CDCClientConnection(CDCAuthenticatorModule& auth_module, mxs::Component* downstream);
    ~CDCClientConnection() = default;

    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

    int32_t write(GWBUF* buffer) override;

    /**
     * Write a string.
     *
     * @param msg String to write
     * @return True on success
     */
    bool write(const char* msg);

    bool init_connection() override;
    void finish_connection() override;

    bool clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply) override;

private:
    int m_state {CDC_STATE_WAIT_FOR_AUTH};      /*< CDC protocol state */

    CDCClientAuthenticator m_authenticator;     /**< Client authentication data */
    mxs::Component*        m_downstream {nullptr}; /**< Downstream component, the session */

    void write_auth_ack();
    void write_auth_err();
};

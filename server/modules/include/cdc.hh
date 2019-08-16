#pragma once

/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/ccdefs.hh>

#include <openssl/sha.h>

#include <maxscale/protocol.hh>
#include <maxscale/protocol2.hh>

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
/**
 * CDC session specific data
 */
struct CDC_session
{
    char         user[CDC_USER_MAXLEN + 1];     /*< username for authentication */
    char         uuid[CDC_UUID_LEN + 1];        /*< client uuid in registration */
    unsigned int flags[2];                      /*< Received flags              */
    uint8_t      auth_data[SHA_DIGEST_LENGTH];  /*< Password Hash               */
    int          state;                         /*< CDC protocol state          */
};

/**
 * CDC protocol
 */
class CDC_protocol : public mxs::ClientProtocol
{
public:
    static MXS_PROTOCOL_SESSION* create(MXS_SESSION* session, mxs::Component* component);
    ~CDC_protocol() = default;

    static char* auth_default();
    static GWBUF* reject(const char* host);

    int32_t read(DCB* dcb) override;
    int32_t write(DCB* dcb, GWBUF* buffer) override;
    int32_t write_ready(DCB* dcb) override;
    int32_t error(DCB* dcb) override;
    int32_t hangup(DCB* dcb) override;

    bool init_connection(DCB* dcb) override;
    void finish_connection(DCB* dcb) override;

    int  state;                     /*< CDC protocol state          */
    char user[CDC_USER_MAXLEN + 1]; /*< username for authentication */
    char type[CDC_TYPE_LEN + 1];    /*< Request Type            */
};

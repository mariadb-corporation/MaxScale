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
#include <maxscale/protocol2.hh>
#include <maxscale/authenticator2.hh>

#define HTTPD_SMALL_BUFFER       1024
#define HTTPD_METHOD_MAXLEN      128
#define HTTPD_REQUESTLINE_MAXLEN 8192

/**
 * HTTPD session specific data
 *
 */
class HTTPDClientProtocol : public mxs::ClientProtocol
{
public:
    HTTPDClientProtocol(std::unique_ptr<mxs::ClientAuthenticator> authenticator);

    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

    int32_t write(DCB* dcb, GWBUF* buffer) override;

    bool init_connection(DCB* dcb) override;
    void finish_connection(DCB* dcb) override;

private:
    std::unique_ptr<mxs::ClientAuthenticator> m_authenticator;  /**< Client authentication data */
};

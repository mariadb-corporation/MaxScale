#pragma once

/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/protocol2.hh>

#define HTTPD_SMALL_BUFFER       1024
#define HTTPD_METHOD_MAXLEN      128
#define HTTPD_REQUESTLINE_MAXLEN 8192

/**
 * HTTPD session specific data
 */
class HTTPDClientConnection : public mxs::ClientConnectionBase
{
public:
    HTTPDClientConnection();

    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

    int32_t write(GWBUF* buffer) override;

    bool init_connection() override;
    void finish_connection() override;
};


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
#include <string>
#include <maxscale/protocol2.hh>


#define MAXSCALED_STATE_LOGIN  1    /**< Waiting for user */
#define MAXSCALED_STATE_PASSWD 2    /**< Waiting for password */
#define MAXSCALED_STATE_DATA   3    /**< User logged in */

/**
 * The maxscaled specific protocol structure to put in the DCB.
 */
class MAXSCALEDClientProtocol : public mxs::ClientProtocol
{
public:
    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

    int32_t write(DCB* dcb, GWBUF* buffer) override;

    bool init_connection(DCB* dcb) override;
    void finish_connection(DCB* dcb) override;

private:
    bool authenticate_socket(DCB* dcb);
    bool authenticate_unix_socket(DCB* generic_dcb);

    int         m_state {MAXSCALED_STATE_LOGIN};/**< The connection state */
    std::string m_username;                     /**< The login name of the user */
};

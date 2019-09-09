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

/**
 * @file maxscaled.h The maxscaled protocol module header file
 */
#include <maxscale/ccdefs.hh>
#include <maxscale/protocol2.hh>

/**
 * The maxscaled specific protocol structure to put in the DCB.
 */
struct MAXSCALED : public mxs::ClientProtocol
{
public:
    static MAXSCALED* create(MXS_SESSION* session, mxs::Component* component);
    MAXSCALED();
    ~MAXSCALED() override;

    static GWBUF* reject(const char* host);

    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

    int32_t write(DCB* dcb, GWBUF* buffer) override;

    bool init_connection(DCB* dcb) override;
    void finish_connection(DCB* dcb) override;

    pthread_mutex_t lock;       /**< Protocol structure lock */
    int             state;      /**< The connection state */
    char*           username;   /**< The login name of the user */
};

#define MAXSCALED_STATE_LOGIN  1    /**< Waiting for user */
#define MAXSCALED_STATE_PASSWD 2    /**< Waiting for password */
#define MAXSCALED_STATE_DATA   3    /**< User logged in */

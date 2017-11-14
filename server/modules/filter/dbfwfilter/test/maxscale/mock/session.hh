#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mock.hh"
#include <maxscale/session.h>
#include "dcb.hh"

namespace maxscale
{

namespace mock
{

/**
 * The class Session provides a mock MXS_SESSION that can be used when
 * testing.
 */
class Session : public MXS_SESSION
{
    Session(const Session&);
    Session& operator = (Session&);

public:
    /**
     * Constructor
     *
     * @param zUser  The client of the session,
     * @param zHost  The host of the client.
     */
    Session(const char* zUser,
            const char* zHost = "127.0.0.1");
    ~Session();

private:
    Dcb m_client_dcb;
};

}

}

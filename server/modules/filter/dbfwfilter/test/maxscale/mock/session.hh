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
#include <maxscale/protocol/mysql.h>
#include "client.hh"

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
     * @param pClient  The client of the session. Must remain valid for
     *                 the lifetime of the Session.
     */
    Session(Client* pClient);
    ~Session();

    Client& client() const;

private:
    Client&       m_client;
    Dcb           m_client_dcb;
    MYSQL_session m_mysql_session;
};

}

}

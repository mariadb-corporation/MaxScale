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

#include "maxscale/mock/session.hh"

namespace maxscale
{

namespace mock
{

Session::Session(Client* pClient)
    : m_client(*pClient)
    , m_client_dcb(this, pClient->user(), pClient->host(), pClient)
{
    MXS_SESSION* pSession = this;

    memset(pSession, 0, sizeof(MXS_SESSION));

    pSession->ses_chk_top = CHK_NUM_SESSION;
    pSession->state = SESSION_STATE_ALLOC;
    pSession->ses_chk_tail = CHK_NUM_SESSION;

    pSession->client_dcb = &m_client_dcb;

    memset(&m_mysql_session, 0, sizeof(m_mysql_session));

    strcpy(m_mysql_session.db, "dummy");

    pSession->variables = new SessionVarsByName;

    m_client_dcb.data = &m_mysql_session;
}

Session::~Session()
{
    delete variables;
}

Client& Session::client() const
{
    return m_client;
}

}

}

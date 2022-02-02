/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#include "nosqlcontext.hh"
#include "nosqlkeys.hh"

std::atomic<int64_t> nosql::Context::s_connection_id;

namespace nosql
{

Context::Context(UserManager* pUm,
                 MXS_SESSION* pSession,
                 ClientConnection* pClient_connection,
                 mxs::Component* pDownstream)
    : m_um(*pUm)
    , m_session(*pSession)
    , m_client_connection(*pClient_connection)
    , m_downstream(*pDownstream)
    , m_connection_id(++s_connection_id)
    , m_sLast_error(std::make_unique<NoError>())
{
}

void Context::get_last_error(DocumentBuilder& doc)
{
    int32_t connection_id = m_connection_id; // MongoDB returns this as a 32-bit integer.

    doc.append(kvp(key::CONNECTION_ID, connection_id));
    m_sLast_error->populate(doc);
    doc.append(kvp(key::OK, 1));
}

void Context::reset_error(int32_t n)
{
    m_sLast_error = std::make_unique<NoError>(n);
}

}

/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mxsmongodatabase.hh"

mxsmongo::Database::Database(const std::string& name, Mongo::Context* pContext)
    : m_name(name)
    , m_context(*pContext)
{
}

mxsmongo::Database::~Database()
{
    mxb_assert(m_state == READY);
}

GWBUF* mxsmongo::Database::run_command(const mxsmongo::Query& req, mxs::Component& downstream)
{
    mxb_assert(is_ready());

    mxb_assert(!true);
    return nullptr;
}

GWBUF* mxsmongo::Database::run_command(const mxsmongo::Msg& req, mxs::Component& downstream)
{
    mxb_assert(is_ready());

    mxb_assert(!true);
    return nullptr;
}

GWBUF* mxsmongo::Database::translate(GWBUF* pMariaDB_response)
{
    mxb_assert(is_pending());

    mxb_assert(!true);
    gwbuf_free(pMariaDB_response);
    return nullptr;
}

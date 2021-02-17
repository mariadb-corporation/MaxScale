/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mirrorbackend.hh"

SMyBackends MyBackend::from_endpoints(const mxs::Endpoints& endpoints)
{
    SMyBackends backends;
    backends.reserve(endpoints.size());

    for (auto e : endpoints)
    {
        backends.emplace_back(new MyBackend(e));
    }

    return backends;
}

bool MyBackend::write(GWBUF* buffer, response_type type)
{
    m_start = Clock::now();
    m_checksum.reset();
    return Backend::write(buffer, type);
}

void MyBackend::process_result(GWBUF* buffer, const mxs::Reply& reply)
{
    m_checksum.update(buffer);
    m_reply = reply;

    if (reply.is_complete())
    {
        m_checksum.finalize();
        m_end = Clock::now();
    }
}

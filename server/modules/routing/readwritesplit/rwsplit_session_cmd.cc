/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "rwsplitsession.hh"

using namespace maxscale;

void RWSplitSession::continue_large_session_write(GWBUF&& querybuf)
{
    for (auto backend : m_raw_backends)
    {
        if (backend->in_use())
        {
            backend->write(querybuf.shallow_clone(), mxs::Backend::NO_RESPONSE);
        }
    }
}

void RWSplitSession::create_one_connection_for_sescmd()
{
    mxb_assert(can_recover_servers());

    // Try to first find a master if we are allowed to connect to one
    if (m_config->master_reconnection && (need_master_for_sescmd() || m_config->master_accept_reads))
    {
        if (auto backend = get_master_backend())
        {
            if (backend->in_use() || prepare_connection(backend))
            {
                if (backend != m_current_master)
                {
                    replace_master(backend);
                }

                MXB_INFO("Chose '%s' as primary due to session write", backend->name());
                return;
            }
        }
    }

    // If no master was found, find a slave
    if (auto backend = get_slave_backend(get_max_replication_lag()))
    {
        if (backend->in_use() || prepare_connection(backend))
        {
            MXB_INFO("Chose '%s' as replica due to session write", backend->name());
        }
    }
}

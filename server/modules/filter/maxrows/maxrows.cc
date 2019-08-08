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

#include "maxrows.hh"

#include <maxscale/modutil.hh>
#include <maxscale/protocol/mysql.hh>

int MaxRowsSession::routeQuery(GWBUF* packet)
{
    return FilterSession::routeQuery(packet);
}

int MaxRowsSession::clientReply(GWBUF* data, DCB* dcb)
{
    mxs::Buffer buffer(data);
    mxs::Reply reply = static_cast<MySQLProtocol*>(dcb->protocol_session())->reply();
    int rv = 1;

    if (m_collect)
    {
        // The resultset is stored in an internal buffer until we know whether to send it or to discard it
        m_buffer.append(buffer.release());

        if (reply.rows_read() > m_instance->config().max_rows || reply.size() > m_instance->config().max_size)
        {
            // A limit was exceeded, discard the result and replace it with a fake result
            switch (m_instance->config().mode)
            {
            case Mode::EMPTY:
                if (reply.rows_read() > 0)
                {
                    // We have the start of the resultset with at least one row in it. Truncate the result
                    // to contain the start of the first resultset with no rows and inject an EOF packet into
                    // it.
                    uint64_t num_packets = reply.field_counts()[0] + 2;
                    auto tmp = mxs::truncate_packets(m_buffer.release(), num_packets);
                    m_buffer.append(tmp);
                    m_buffer.append(modutil_create_eof(num_packets + 1));
                    m_collect = false;
                }
                break;

            case Mode::ERR:
                m_buffer.reset(
                    modutil_create_mysql_err_msg(1, 0, 1226, "42000",
                                                 reply.rows_read() > m_instance->config().max_rows ?
                                                 "Resultset row limit exceeded" :
                                                 "Resultset size limit exceeded"));
                m_collect = false;
                break;

            case Mode::OK:
                m_buffer.reset(modutil_create_ok());
                m_collect = false;
                break;

            default:
                mxb_assert(!true);
                rv = 0;
                break;
            }
        }
    }

    if (reply.is_complete())
    {
        rv = FilterSession::clientReply(m_buffer.release(), dcb);
        m_collect = true;
    }

    return rv;
}

extern "C"
{
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "A filter that limits resultsets.",
        "V1.0.0",
        MaxRows::CAPABILITIES,
        &MaxRows::s_object,
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {
                "max_resultset_rows",
                MXS_MODULE_PARAM_COUNT,
                MXS_MODULE_PARAM_COUNT_MAX
            },
            {
                "max_resultset_size",
                MXS_MODULE_PARAM_SIZE,
                "65536"
            },
            {
                "debug",
                MXS_MODULE_PARAM_COUNT,
                "0",
                MXS_MODULE_OPT_DEPRECATED
            },
            {
                "max_resultset_return",
                MXS_MODULE_PARAM_ENUM,
                "empty",
                MXS_MODULE_OPT_ENUM_UNIQUE,
                mode_values
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}

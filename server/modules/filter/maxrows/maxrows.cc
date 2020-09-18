/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include "maxrows.hh"

#include <maxscale/modutil.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

namespace
{

namespace maxrows
{

namespace config = mxs::config;

config::Specification specification(MXS_MODULE_NAME, config::Specification::FILTER);

config::ParamCount max_resultset_rows(
    &specification,
    "max_resultset_rows",
    "Specifies the maximum number of rows a resultset can have in order to be returned to the user.",
    std::numeric_limits<uint32_t>::max());

config::ParamSize max_resultset_size(
    &specification,
    "max_resultset_size",
    "Specifies the maximum size a resultset can have in order to be sent to the client.",
    65536);

config::ParamInteger debug(
    &specification,
    "debug",
    "An integer value, using which the level of debug logging made by the Maxrows "
    "filter can be controlled.",
    0,
    0,
    3);

config::ParamEnum<MaxRowsConfig::Mode> max_resultset_return(
    &specification,
    "max_resultset_return",
    "Specifies what the filter sends to the client when the rows or size limit "
    "is hit; an empty packet, an error packet or an ok packet.",
    {
        { MaxRowsConfig::Mode::EMPTY, "empty" },
        { MaxRowsConfig::Mode::ERR,   "error" },
        { MaxRowsConfig::Mode::OK,    "ok" }
    },
    MaxRowsConfig::Mode::EMPTY);
}
}

MaxRowsConfig::MaxRowsConfig(const char* zName)
    : mxs::config::Configuration(zName, &maxrows::specification)
{
    add_native(&MaxRowsConfig::max_rows, &maxrows::max_resultset_rows);
    add_native(&MaxRowsConfig::max_size, &maxrows::max_resultset_size);
    add_native(&MaxRowsConfig::debug, &maxrows::debug);
    add_native(&MaxRowsConfig::mode, &maxrows::max_resultset_return);
}

int MaxRowsSession::routeQuery(GWBUF* packet)
{
    return FilterSession::routeQuery(packet);
}

int MaxRowsSession::clientReply(GWBUF* data, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxs::Buffer buffer(data);
    int rv = 1;

    if (m_collect)
    {
        // The resultset is stored in an internal buffer until we know whether to send it or to discard it
        m_buffer.append(buffer.release());

        if (reply.rows_read() > (uint64_t)m_instance->config().max_rows
            || reply.size() > (uint64_t)m_instance->config().max_size)
        {
            // A limit was exceeded, discard the result and replace it with a fake result
            switch (m_instance->config().mode)
            {
            case MaxRowsConfig::Mode::EMPTY:
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

            case MaxRowsConfig::Mode::ERR:
                m_buffer.reset(
                    modutil_create_mysql_err_msg(1, 0, 1226, "42000",
                                                 reply.rows_read() > (uint64_t)m_instance->config().max_rows ?
                                                 "Resultset row limit exceeded" :
                                                 "Resultset size limit exceeded"));
                m_collect = false;
                break;

            case MaxRowsConfig::Mode::OK:
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
        rv = FilterSession::clientReply(m_buffer.release(), down, reply);
        m_collect = true;
    }

    return rv;
}

//static
MaxRows* MaxRows::create(const char* name, mxs::ConfigParameters* params)
{
    MaxRows* filter = nullptr;
    Config config(name);

    if (config.configure(*params))
    {
        filter = new(std::nothrow) MaxRows(name, std::move(config));
    }

    return filter;
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
        nullptr,       /* Process init. */
        nullptr,       /* Process finish. */
        nullptr,       /* Thread init. */
        nullptr,       /* Thread finish. */
        {{nullptr}},
        &maxrows::specification
    };

    return &info;
}
}

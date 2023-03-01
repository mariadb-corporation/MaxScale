/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
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

config::Specification specification(MXB_MODULE_NAME, config::Specification::FILTER);

config::ParamCount max_resultset_rows(
    &specification,
    "max_resultset_rows",
    "Specifies the maximum number of rows a resultset can have in order to be returned to the user.",
    std::numeric_limits<uint32_t>::max(),
    config::Param::AT_RUNTIME);

config::ParamSize max_resultset_size(
    &specification,
    "max_resultset_size",
    "Specifies the maximum size a resultset can have in order to be sent to the client.",
    65536,
    config::Param::AT_RUNTIME);

config::ParamInteger debug(
    &specification,
    "debug",
    "An integer value, using which the level of debug logging made by the Maxrows "
    "filter can be controlled.",
    0,
    0,
    3,
    config::Param::AT_RUNTIME);

config::ParamEnum<MaxRowsConfig::Mode> max_resultset_return(
    &specification,
    "max_resultset_return",
    "Specifies what the filter sends to the client when the rows or size limit "
    "is hit; an empty packet, an error packet or an ok packet.",
        {
            {MaxRowsConfig::Mode::EMPTY, "empty"},
            {MaxRowsConfig::Mode::ERR, "error"},
            {MaxRowsConfig::Mode::OK, "ok"}
        },
    MaxRowsConfig::Mode::EMPTY,
    config::Param::AT_RUNTIME);
}

// See: https://mariadb.com/kb/en/library/eof_packet/
GWBUF* modutil_create_eof(uint8_t seq)
{
    uint8_t eof[] = {0x5, 0x0, 0x0, seq, 0xfe, 0x0, 0x0, 0x0, 0x0};
    return gwbuf_alloc_and_load(sizeof(eof), eof);
}

GWBUF* truncate_packets(GWBUF* buffer, uint64_t packets)
{
    auto it = buffer->begin();
    size_t total_bytes = buffer->length();
    size_t bytes_used = 0;

    while (it != buffer->end())
    {
        size_t bytes_left = total_bytes - bytes_used;

        if (bytes_left < MYSQL_HEADER_LEN)
        {
            // Partial header
            break;
        }

        // Extract packet length and command byte
        uint32_t len = *it++;
        len |= (*it++) << 8;
        len |= (*it++) << 16;
        ++it;   // Skip the sequence

        if (bytes_left < len + MYSQL_HEADER_LEN)
        {
            // Partial packet payload
            break;
        }

        bytes_used += len + MYSQL_HEADER_LEN;

        mxb_assert(it != buffer->end());
        it += len;

        if (--packets == 0)
        {
            // Trim off the extra data at the end
            buffer->rtrim(std::distance(it, buffer->end()));
            break;
        }
    }

    return buffer;
}
}

MaxRowsConfig::MaxRowsConfig(const char* zName)
    : mxs::config::Configuration(zName, &maxrows::specification)
    , max_rows(this, &maxrows::max_resultset_rows)
    , max_size(this, &maxrows::max_resultset_size)
    , debug(this, &maxrows::debug)
    , mode(this, &maxrows::max_resultset_return)
{
}

MaxRowsSession::MaxRowsSession(MXS_SESSION* pSession, SERVICE* pService, MaxRows* pFilter)
    : FilterSession(pSession, pService)
    , m_max_rows(pFilter->config().max_rows.get())
    , m_max_size(pFilter->config().max_size.get())
    , m_debug(pFilter->config().debug.get())
    , m_mode(pFilter->config().mode.get())
{
}

bool MaxRowsSession::routeQuery(GWBUF&& packet)
{
    return FilterSession::routeQuery(std::move(packet));
}

bool MaxRowsSession::clientReply(GWBUF&& buf, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxs::Buffer buffer(mxs::gwbuf_to_gwbufptr(std::move(buf)));
    int rv = 1;

    if (m_collect)
    {
        // The resultset is stored in an internal buffer until we know whether to send it or to discard it
        m_buffer.append(buffer.release());

        if (reply.rows_read() > m_max_rows || reply.size() > m_max_size)
        {
            // A limit was exceeded, discard the result and replace it with a fake result
            switch (m_mode)
            {
            case MaxRowsConfig::Mode::EMPTY:
                if (reply.rows_read() > 0)
                {
                    // We have the start of the resultset with at least one row in it. Truncate the result
                    // to contain the start of the first resultset with no rows and inject an EOF packet into
                    // it.
                    uint64_t num_packets = reply.field_counts()[0] + 2;
                    auto tmp = truncate_packets(m_buffer.release(), num_packets);
                    m_buffer.append(tmp);
                    m_buffer.append(modutil_create_eof(num_packets + 1));
                    m_collect = false;
                }
                break;

            case MaxRowsConfig::Mode::ERR:
                m_buffer.reset(
                    mariadb::create_error_packet_ptr(1, 1226, "42000",
                                                     reply.rows_read() > m_max_rows ?
                                                     "Resultset row limit exceeded" :
                                                     "Resultset size limit exceeded"));
                m_collect = false;
                break;

            case MaxRowsConfig::Mode::OK:
                m_buffer.reset(mxs::gwbuf_to_gwbufptr(mariadb::create_ok_packet()));
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
        rv = FilterSession::clientReply(mxs::gwbufptr_to_gwbuf(m_buffer.release()), down, reply);
        m_collect = true;
    }

    return rv;
}

// static
MaxRows* MaxRows::create(const char* name)
{
    return new MaxRows(name);
}

extern "C"
{
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "A filter that limits resultsets.",
        "V1.0.0",
        MaxRows::CAPABILITIES,
        &mxs::FilterApi<MaxRows>::s_api,
        nullptr,        /* Process init. */
        nullptr,        /* Process finish. */
        nullptr,        /* Thread init. */
        nullptr,        /* Thread finish. */
        &maxrows::specification
    };

    return &info;
}
}

/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/ps_to_text.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxbase/string.hh>
#include <maxsql/mariadb.hh>
#include <maxsimd/canonical.hh>

namespace
{
// https://mariadb.com/kb/en/result-set-packets/#field-types
enum : uint8_t
{
    MYSQL_TYPE_DECIMAL     = 0,
    MYSQL_TYPE_TINY        = 1,
    MYSQL_TYPE_SHORT       = 2,
    MYSQL_TYPE_LONG        = 3,
    MYSQL_TYPE_FLOAT       = 4,
    MYSQL_TYPE_DOUBLE      = 5,
    MYSQL_TYPE_NULL        = 6,
    MYSQL_TYPE_TIMESTAMP   = 7,
    MYSQL_TYPE_LONGLONG    = 8,
    MYSQL_TYPE_INT24       = 9,
    MYSQL_TYPE_DATE        = 10,
    MYSQL_TYPE_TIME        = 11,
    MYSQL_TYPE_DATETIME    = 12,
    MYSQL_TYPE_YEAR        = 13,
    MYSQL_TYPE_NEWDATE     = 14,
    MYSQL_TYPE_VARCHAR     = 15,
    MYSQL_TYPE_BIT         = 16,
    MYSQL_TYPE_TIMESTAMP2  = 17,
    MYSQL_TYPE_DATETIME2   = 18,
    MYSQL_TYPE_TIME2       = 19,
    MYSQL_TYPE_JSON        = 245,
    MYSQL_TYPE_NEWDECIMAL  = 246,
    MYSQL_TYPE_ENUM        = 247,
    MYSQL_TYPE_SET         = 248,
    MYSQL_TYPE_TINY_BLOB   = 249,
    MYSQL_TYPE_MEDIUM_BLOB = 250,
    MYSQL_TYPE_LONG_BLOB   = 251,
    MYSQL_TYPE_BLOB        = 252,
    MYSQL_TYPE_VAR_STRING  = 253,
    MYSQL_TYPE_STRING      = 254,
    MYSQL_TYPE_GEOMETRY    = 255,
};

std::string binary_to_text(const uint8_t** data, uint8_t type, uint8_t is_unsigned)
{
    const uint8_t* ptr = *data;

    switch (type)
    {
    case MYSQL_TYPE_DOUBLE:
        // https://mariadb.com/kb/en/resultset-row/#double-binary-encoding
        return "TODO: implement";

    case MYSQL_TYPE_FLOAT:
        // https://mariadb.com/kb/en/resultset-row/#float-binary-encoding
        return "TODO: implement";

    case MYSQL_TYPE_LONGLONG:
        // https://mariadb.com/kb/en/resultset-row/#bigint-binary-encoding
        return "TODO: implement";

    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_INT24:
        // https://mariadb.com/kb/en/resultset-row/#integer-binary-encoding
        return "TODO: implement";

    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_YEAR:
        // https://mariadb.com/kb/en/resultset-row/#smallint-binary-encoding
        return "TODO: implement";

    case MYSQL_TYPE_TINY:
        // https://mariadb.com/kb/en/resultset-row/#tinyint-binary-encoding
        return "TODO: implement";

    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_NEWDATE:
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_DATETIME2:
    case MYSQL_TYPE_TIMESTAMP:
    case MYSQL_TYPE_TIMESTAMP2:
        // https://mariadb.com/kb/en/resultset-row/#timestamp-binary-encoding
        return "TODO: implement";

    case MYSQL_TYPE_TIME2:
    case MYSQL_TYPE_TIME:
        // https://mariadb.com/kb/en/resultset-row/#time-binary-encoding
        return "TODO: implement";

    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_BIT:
    case MYSQL_TYPE_JSON:
    case MYSQL_TYPE_NEWDECIMAL:
    case MYSQL_TYPE_ENUM:
    case MYSQL_TYPE_SET:
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_VAR_STRING:
    case MYSQL_TYPE_STRING:
    case MYSQL_TYPE_GEOMETRY:
        // https://mariadb.com/kb/en/protocol-data-types/#length-encoded-bytes
        return "TODO: implement";

    case MYSQL_TYPE_NULL:   // Never used
    default:
        mxb_assert(!true);
        return "NULL";
    }
}

bool bit_is_set(const uint8_t* ptr, int bit)
{
    return ptr[bit / 8] & (1 << (bit % 8));
}
}

namespace mariadb
{
void PsToText::track_query(const GWBUF& buffer)
{
    switch (get_command(buffer))
    {
    case MXS_COM_STMT_PREPARE:
        // Technically we could parse the COM_STMT_PREPARE here and not have to do anything in track_reply().
        // The only problem is that there's a corner case where a client repeatedly executes prepared
        // statements that end up failing. In this case the PS map would keep growing. This could be solved by
        // optimistically storing the PS and then in track_reply() only removing failed ones but the practical
        // difference in it is not significant enough to warrant it.
        m_queue.push_back(buffer.shallow_clone());
        break;

    case MXS_COM_STMT_CLOSE:
        m_ps.erase(mxs_mysql_extract_ps_id(buffer));
        break;

    case MXS_COM_STMT_RESET:
        // TODO: This should reset any data that was read from a COM_STMT_SEND_LONG_DATA
        break;

    default:
        break;
    }
}

void PsToText::track_reply(const mxs::Reply& reply)
{
    if (reply.is_complete() && reply.command() == MXS_COM_STMT_PREPARE)
    {
        mxb_assert(!m_queue.empty());
        const GWBUF& buffer = m_queue.front();
        mxb_assert(get_command(buffer) == MXS_COM_STMT_PREPARE);

        if (!reply.error())
        {
            // Calculate the parameter offsets that are used by maxsimd::canonical_args_to_sql().
            std::string sql(get_sql(buffer));
            const char* ptr = sql.c_str();
            const char* end = ptr + sql.size();

            std::vector<uint32_t> param_offsets;
            param_offsets.reserve(reply.param_count());

            while ((ptr = mxb::strnchr_esc_mariadb(ptr, end, '?')))
            {
                param_offsets.push_back(std::distance(sql.c_str(), ptr++));
            }

            mxb_assert(reply.param_count() == param_offsets.size());

            if (reply.param_count() == param_offsets.size())
            {
                m_ps.emplace(buffer.id(), Prepare {std::move(sql), std::move(param_offsets)});
            }
            else
            {
                MXB_ERROR("Placeholder count in '%s' was calculated as %lu "
                          "but the server reports it as %hu.", sql.c_str(),
                          param_offsets.size(), reply.param_count());
            }
        }

        m_queue.pop_front();
    }
}

std::string PsToText::to_sql(const GWBUF& buffer) const
{
    std::string rval;

    switch (get_command(buffer))
    {
    case MXS_COM_QUERY:
        rval.assign(get_sql(buffer));
        break;

    case MXS_COM_STMT_EXECUTE:
        if (auto it = m_ps.find(mxs_mysql_extract_ps_id(buffer)); it != m_ps.end())
        {
            auto args = convert_params_to_text(it->second, buffer);
            rval = maxsimd::canonical_args_to_sql(it->second.sql, args);
        }
        break;

    default:
        break;
    }

    return rval;
}

std::pair<std::string_view, maxsimd::CanonicalArgs> PsToText::get_args(const GWBUF& buffer) const
{
    if (get_command(buffer) == MXS_COM_STMT_EXECUTE)
    {
        if (auto it = m_ps.find(mxs_mysql_extract_ps_id(buffer)); it != m_ps.end())
        {
            return std::pair<std::string_view, maxsimd::CanonicalArgs>(
                it->second.sql, convert_params_to_text(it->second, buffer));
        }
    }

    return {};
}

std::string PsToText::get_prepare(const GWBUF& buffer) const
{
    std::string rval;

    if (get_command(buffer) == MXS_COM_STMT_EXECUTE)
    {
        if (auto it = m_ps.find(mxs_mysql_extract_ps_id(buffer)); it != m_ps.end())
        {
            rval = it->second.sql;
        }
    }

    return rval;
}

maxsimd::CanonicalArgs PsToText::convert_params_to_text(const Prepare& ps, const GWBUF& buffer) const
{
    size_t param_count = ps.param_offsets.size();

    if (param_count == 0)
    {
        // The prepared statement had no parameters
        return {};
    }

    const uint8_t* ptr = buffer.data()
        + MYSQL_HEADER_LEN  // Packet header
        + 1                 // Command byte
        + 4                 // Statement ID
        + 1                 // Flags
        + 4;                // Iteration count (always 1)

    // https://mariadb.com/kb/en/com_stmt_execute/#null-bitmap
    auto null_bitmap = ptr;
    ptr += (param_count + 7) / 8;


    bool send_types = *ptr++;
    auto type_ptr = ptr;

    if (send_types)
    {
        // Two bytes per parameter
        size_t n = param_count * 2;
        // This needs to be stored if the same COM_STMT_PREPARE is used more than once.
        const_cast<Prepare&>(ps).type_info.assign(ptr, ptr + n);
        ptr += n;
    }
    else
    {
        mxb_assert(!ps.type_info.empty());
        type_ptr = ps.type_info.data();
    }

    maxsimd::CanonicalArgs args;
    args.reserve(param_count);

    for (size_t i = 0; i < param_count; i++)
    {
        maxsimd::CanonicalArgument arg;
        uint8_t type = type_ptr[i * 2];
        uint8_t is_unsigned = type_ptr[i * 2 + 1];
        arg.value = bit_is_set(null_bitmap, i) ? "NULL" : binary_to_text(&ptr, type, is_unsigned);
        arg.pos = ps.param_offsets[i];
        args.emplace_back(std::move(arg));
    }

    return args;
}
}

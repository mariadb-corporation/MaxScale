/*
 * Copyright (c) 2023 MariaDB plc
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

#include "postgresprotocol.hh"
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include "pgprotocolmodule.hh"
#include "pgparser.hh"

namespace
{

struct ThisUnit
{
    PgParser* pParser = nullptr;
} this_unit;

int module_init()
{
    mxb_assert(!this_unit.pParser);

    auto& pp = MariaDBParser::get().plugin();

    this_unit.pParser = new PgParser(pp.create_parser(&PgParser::Helper::get()));

    return 0;
}

void module_finish()
{
    mxb_assert(this_unit.pParser);
    delete this_unit.pParser;
}

}

PgParser& PgParser::get()
{
    mxb_assert(this_unit.pParser);

    return *this_unit.pParser;
}

namespace postgres
{

const char* backend_command_to_str(uint8_t cmd)
{
    switch (cmd)
    {
    case AUTHENTICATION:
        return "Authentication";

    case BACKEND_KEY_DATA:
        return "BackendKeyData";

    case BIND_COMPLETE:
        return "BindComplete";

    case CLOSE_COMPLETE:
        return "CloseComplete";

    case COMMAND_COMPLETE:
        return "CommandComplete";

    case COPY_BOTH_RESPONSE:
        return "CopyBothResponse";

    case COPY_IN_RESPONSE:
        return "CopyInResponse";

    case COPY_OUT_RESPONSE:
        return "CopyOutResponse";

    case DATA_ROW:
        return "DataRow";

    case EMPTY_QUERY_RESPONSE:
        return "EmptyQueryResponse";

    case ERROR_RESPONSE:
        return "ErrorResponse";

    case NEGOTIATE_PROTOCOL_VERSION:
        return "NegotiateProtocolVersion";

    case FUNCTION_CALL_RESPONSE:
        return "FunctionCallResponse";

    case NO_DATA:
        return "NoData";

    case NOTICE_RESPONSE:
        return "NoticeResponse";

    case NOTIFICATION_RESPONSE:
        return "NotificationResponse";

    case PARAMETER_DESCRIPTION:
        return "ParameterDescription";

    case PARAMETER_STATUS:
        return "ParameterStatus";

    case PARSE_COMPLETE:
        return "ParseComplete";

    case PORTAL_SUSPENDED:
        return "PortalSuspended";

    case READY_FOR_QUERY:
        return "ReadyForQuery";

    case ROW_DESCRIPTION:
        return "RowDescription";
    }

    mxb_assert(!true);
    thread_local char buffer[20];
    snprintf(buffer, sizeof(buffer), "Unknown: 0x%hhx", cmd);
    return buffer;
}

const char* client_command_to_str(uint8_t cmd)
{
    switch (cmd)
    {
    case BIND:
        return "Bind";

    case CLOSE:
        return "Close";

    case COPY_FAIL:
        return "CopyFail";

    case DESCRIBE:
        return "Describe";

    case EXECUTE:
        return "Execute";

    case FLUSH:
        return "Flush";

    case PARSE:
        return "Parse";

    case QUERY:
        return "Query";

        // SASL_INITIAL_RESPONSE, SASL_RESPONSE, GSS_RESPONSE share the same value. There aren't seen after
        // the authentication has completed.
    case PASSWORD_MESSAGE:
        return "Auth";

    case SYNC:
        return "Sync";

    case TERMINATE:
        return "Terminate";
    }

    mxb_assert(!true);
    thread_local char buffer[20];
    snprintf(buffer, sizeof(buffer), "Unknown: 0x%hhx", cmd);
    return buffer;
}

std::tuple<bool, GWBUF> read_packet(DCB* dcb, ExpectCmdByte expect_cmd_byte)
{
    size_t len_offset = expect_cmd_byte == ExpectCmdByte::YES ? 1 : 0;
    size_t min_bytes = expect_cmd_byte == ExpectCmdByte::YES ? HEADER_LEN : HEADER_LEN - 1;

    auto res = dcb->read(min_bytes, 0);
    auto& [ok, buf] = res;

    if (ok && buf)
    {
        uint32_t len = get_uint32(buf.data() + len_offset);
        if (expect_cmd_byte == ExpectCmdByte::YES)
        {
            len += 1;
        }

        if (buf.length() < len)
        {
            // Incomplete packet, put it back in the buffer
            // TODO: The packets can be very big. Figure out how to deal with very large packets.
            dcb->unread(std::move(buf));
            buf.clear();
        }
        else if (buf.length() > len)
        {
            // Too much data. Put the remaining back into the DCB.
            GWBUF tmp = buf.split(len);
            dcb->unread(std::move(buf));
            buf = std::move(tmp);
        }
    }

    return res;
}

std::map<uint8_t, std::string_view> extract_response_fields(const uint8_t* buffer, size_t size)
{
    mxb_assert(size > 0);
    mxb_assert(buffer[0] == pg::ERROR_RESPONSE || buffer[0] == pg::NOTICE_RESPONSE);

    const uint8_t* ptr = buffer + 1;
    uint32_t len = pg::get_uint32(ptr);
    const uint8_t* end = ptr + len;
    mxb_assert(end == buffer + size);
    ptr += 4;

    std::map<uint8_t, std::string_view> rval;

    // The ErrorResponse and NoticeResponse are a list of values, each consisting of a one byte "field type"
    // value followed by a null-terminated string. To extract all the information, the payload must be
    // iterated through until a field type of 0 is found. The field descriptions can be found here:
    // https://www.postgresql.org/docs/current/protocol-error-fields.html

    while (ptr < end && *ptr)
    {
        // Field type
        uint8_t type = *ptr++;

        // Null-terminated string
        auto str = reinterpret_cast<const char*>(ptr);
        size_t len = strnlen(str, end - ptr);
        rval[type] = std::string_view(str, len);
        ptr += len + 1;
    }

    return rval;
}

std::string format_response(const GWBUF& buffer)
{
    auto values = extract_response_fields(buffer.data(), buffer.length());

    std::string_view severity = values.count('V') ? values['V'] : values['S'];
    std::string_view msg = values['M'];
    std::string_view sqlstate = values['C'];
    std::string_view detail = values['D'];
    std::string_view hint = values['H'];

    return mxb::rtrimmed_copy(mxb::cat(severity, ": ", sqlstate, " ", msg, " ", detail, " ", hint));
}

bool will_respond(const GWBUF& buffer)
{
    switch (buffer[0])
    {
    case pg::BIND:
    case pg::CLOSE:
    case pg::DESCRIBE:
    case pg::EXECUTE:
    case pg::FLUSH:
    case pg::PARSE:
        return false;

    default:
        return true;
    }
}

GWBUF create_query_packet(std::string_view sql)
{
    GWBUF buf{pg::HEADER_LEN + sql.size() + 1};
    auto ptr = buf.data();

    *ptr++ = pg::QUERY;
    ptr += pg::set_uint32(ptr, buf.length() - 1);
    memcpy(ptr, sql.data(), sql.size());
    ptr += sql.size();
    *ptr = 0x0;

    return buf;
}

std::string_view get_sql(const GWBUF& packet)
{
    std::string_view rv;

    if (packet.length() > pg::HEADER_LEN)
    {
        auto ptr = packet.data();

        if (*ptr++ == pg::QUERY)
        {
            uint32_t len = pg::get_uint32(ptr) - 4; // Exclude the 4 bytes of length information.

            if (pg::HEADER_LEN + len == packet.length())
            {
                ptr += 4; // Skip the 4 bytes of length information.

                if (ptr[len - 1] == 0)
                {
                    rv = std::string_view { reinterpret_cast<const char*>(ptr), len - 1 };
                }
                else
                {
                    MXB_ERROR("Invalid Query packet; missing terminating NULL.");
                }
            }
            else
            {
                MXB_ERROR("Invalid Query packet; packet claims to be %lu bytes, but "
                          "packet is %lu bytes.",
                          (unsigned long) pg::HEADER_LEN + len,
                          (unsigned long) packet.length());
            }
        }
    }

    return rv;
}

bool is_prepare(const GWBUF& packet)
{
    return packet.length() > 0 && packet[0] == pg::PARSE;
}

}

/**
 * postgresprotocol module entry point.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::PROTOCOL,
        mxs::ModuleStatus::IN_DEVELOPMENT,
        MXS_PROTOCOL_VERSION,
        "Postgres client protocol implementation",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::ProtocolApiGenerator<PgProtocolModule>::s_api,
        module_init,
        module_finish,
        nullptr,
        nullptr,
        &PgConfiguration::specification()
    };

    return &info;
}

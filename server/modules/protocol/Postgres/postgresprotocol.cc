/*
 * Copyright (c) 2023 MariaDB plc
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

#include "postgresprotocol.hh"
#include <maxscale/config.hh>
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include <maxbase/pretty_print.hh>
#include "pgprotocolmodule.hh"
#include "pgparser.hh"

namespace
{

int module_init()
{
    return 0;
}

void module_finish()
{
}
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

    case COPY_DATA:
        return "CopyData";

    case COPY_DONE:
        return "CopyDone";
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

    case COPY_DATA:
        return "CopyData";

    case COPY_DONE:
        return "CopyDone";
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
        len = strnlen(str, end - ptr);
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
    return will_respond(buffer[0]);
}

bool will_respond(uint8_t cmd)
{
    switch (cmd)
    {
    case pg::BIND:
    case pg::CLOSE:
    case pg::DESCRIBE:
    case pg::EXECUTE:
    case pg::FLUSH:
    case pg::PARSE:
    case pg::COPY_DATA:
    case pg::COPY_DONE:
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
        uint8_t cmd = *ptr++;

        if (cmd == pg::QUERY)
        {
            uint32_t len = pg::get_uint32(ptr) - 4;     // Exclude the 4 bytes of length information.

            if (pg::HEADER_LEN + len == packet.length())
            {
                ptr += 4;   // Skip the 4 bytes of length information.

                if (ptr[len - 1] == 0)
                {
                    rv = std::string_view {reinterpret_cast<const char*>(ptr), len - 1};
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
        else if (cmd == pg::PARSE)
        {
            ptr += 4;   // Ignore the length

            if (auto id_end = std::find(ptr, packet.end(), 0); id_end != packet.end())
            {
                ++id_end;

                if (auto sql_end = std::find(id_end, packet.end(), 0); sql_end != packet.end())
                {
                    rv = std::string_view{reinterpret_cast<const char*>(id_end),
                                          static_cast<size_t>(sql_end - id_end)};
                }
            }
        }
    }

    return rv;
}

bool is_prepare(const GWBUF& packet)
{
    return packet.length() > 0 && packet[0] == pg::PARSE;
}

bool is_query(const GWBUF& packet)
{
    return packet.length() > 0 && packet[0] == pg::QUERY;
}

std::string describe(const GWBUF& packet, int max_len)
{
    std::ostringstream ss;
    const uint8_t* ptr = packet.data();

    char cmd = *ptr++;
    uint32_t len = pg::get_uint32(ptr);
    ptr += 4;
    ss << pg::client_command_to_str(cmd) << " (" << mxb::pretty_size(len) << ")";

    switch (cmd)
    {
    case pg::QUERY:
        ss << " stmt: " << pg::get_string(ptr).substr(0, max_len);
        break;

    case pg::PARSE:
        {
            auto id = pg::get_string(ptr);
            ptr += id.size() + 1;
            ss << " id: '" << id << "' stmt: " << pg::get_string(ptr).substr(0, max_len);
        }
        break;

    case pg::CLOSE:
    case pg::DESCRIBE:
        {
            char type = *ptr++;
            ss << " type: '" << type << "' id: '" << pg::get_string(ptr) << "'";
        }
        break;

    case pg::EXECUTE:
        ss << " id: '" << pg::get_string(ptr) << "'";
        break;

    case pg::BIND:
        {
            auto portal = pg::get_string(ptr);
            ptr += portal.size() + 1;
            ss << " portal: '" << portal << "' id: '" << pg::get_string(ptr) << "'";
        }
        break;

    default:
        break;
    }

    return ss.str();
}

GWBUF make_error(Severity sev, std::string_view sqlstate, std::string_view msg)
{
    std::string severity = (sev == Severity::ERROR) ? "ERROR" : "FATAL";
    // The field type explanations are here
    // https://www.postgresql.org/docs/current/protocol-error-fields.html
    auto old_severity = mxb::cat("S", severity);
    auto new_severity = mxb::cat("V", severity);
    auto code = mxb::cat("C", sqlstate);
    auto message = mxb::cat("M", msg);

    GWBUF buf{pg::HEADER_LEN
              + old_severity.size() + 1
              + new_severity.size() + 1
              + code.size() + 1
              + message.size() + 1
              + 1};
    auto ptr = buf.data();

    *ptr++ = 'E';
    ptr += pg::set_uint32(ptr, buf.length() - 1);
    ptr += pg::set_string(ptr, old_severity);
    ptr += pg::set_string(ptr, new_severity);
    ptr += pg::set_string(ptr, code);
    ptr += pg::set_string(ptr, message);
    *ptr = 0;
    return buf;
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

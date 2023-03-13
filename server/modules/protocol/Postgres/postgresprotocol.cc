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
#include "pgprotocolmodule.hh"

namespace postgres
{
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
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &PgConfiguration::specification()
    };

    return &info;
}

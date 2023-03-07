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

std::string format_response(const GWBUF& buffer)
{
    mxb_assert(buffer[0] == pg::ERROR_RESPONSE || buffer[0] == pg::NOTICE_RESPONSE);

    const uint8_t* ptr = buffer.data() + 1;
    uint32_t len = pg::get_uint32(ptr);
    const uint8_t* end = ptr + len;
    mxb_assert(end == buffer.end());
    ptr += 4;

    std::string_view severity;
    std::string_view msg;
    std::string_view sqlstate;
    std::string_view detail;
    std::string_view hint;

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
        std::string_view value(str, len);
        ptr += len + 1;

        switch (type)
        {
        case 'M':   // Message
            msg = value;
            break;

        case 'C':   // SQLSTATE
            sqlstate = value;
            break;

        case 'D':   // Detailed error
            detail = value;
            break;

        case 'H':   // Hint
            hint = value;
            break;

        case 'S':   // Severity
            // This is an older version of the severity which might be localized. Prefer the non-localized
            // version if one exists.
            if (severity.empty())
            {
                severity = value;
            }
            break;

        case 'V':   // Non-localized severity
            severity = value;
            break;

        default:
            // Something else, just ignore it.
            break;
        }
    }

    return mxb::rtrimmed_copy(mxb::cat(severity, ": ", sqlstate, " ", msg, " ", detail, " ", hint));
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

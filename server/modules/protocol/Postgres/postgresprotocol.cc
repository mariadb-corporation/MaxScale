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
std::tuple<bool, GWBUF> read_packet(DCB* dcb)
{
    auto res = dcb->read(HEADER_LEN, 0);
    auto& [ok, buf] = res;

    if (ok && buf)
    {
        uint32_t len = get_uint32(buf.data() + 1);

        if (buf.length() < len + 1)
        {
            // Incomplete packet, put it back in the buffer
            // TODO: The packets can be very big. Figure out how to deal with very large packets.
            dcb->unread(std::move(buf));
            buf.clear();
        }
        else if (buf.length() > len + 1)
        {
            // Too much data. Put the remaining back into the DCB.
            GWBUF tmp = buf.split(len + 1);
            dcb->unread(std::move(buf));
            buf = std::move(tmp);
        }
    }

    return res;
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

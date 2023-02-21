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

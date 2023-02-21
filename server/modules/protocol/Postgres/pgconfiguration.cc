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

#include "pgconfiguration.hh"


namespace
{
namespace postgresprotocol
{

// Use the module name as the configuration prefix
const char* CONFIG_PREFIX = MXB_MODULE_NAME;

mxs::config::Specification specification(MXB_MODULE_NAME, mxs::config::Specification::PROTOCOL,
                                         CONFIG_PREFIX);
}
}

PgConfiguration::PgConfiguration(const std::string& name, PgProtocolModule* pInstance)
    : mxs::config::Configuration(name, &postgresprotocol::specification)
    , m_instance(*pInstance)
{
}

// static
mxs::config::Specification& PgConfiguration::specification()
{
    return postgresprotocol::specification;
}

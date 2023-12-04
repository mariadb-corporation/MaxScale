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

#include "pgconfiguration.hh"
#include "pgprotocolmodule.hh"


namespace
{
namespace postgresprotocol
{

// Use the module name as the configuration prefix
const char* CONFIG_PREFIX = MXB_MODULE_NAME;

mxs::config::Specification specification(MXB_MODULE_NAME, mxs::config::Specification::PROTOCOL,
                                         CONFIG_PREFIX);

mxs::config::ParamString parser(
    &specification,
    "parser",
    "What parser the Postgres protocol module should use. If 'mariadb' "
    "then the one used by 'mariadbprotocol'.",
    PgConfiguration::PP_PG_QUERY);

}
}

PgConfiguration::PgConfiguration(const std::string& name, PgProtocolModule* pInstance)
    : mxs::config::Configuration(name, &postgresprotocol::specification)
    , m_instance(*pInstance)
{
    add_native(&PgConfiguration::parser, &postgresprotocol::parser);
}

// static
mxs::config::Specification& PgConfiguration::specification()
{
    return postgresprotocol::specification;
}

bool PgConfiguration::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    return m_instance.post_configure();
}

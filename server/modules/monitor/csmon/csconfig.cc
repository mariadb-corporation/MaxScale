/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "csconfig.hh"

namespace
{

namespace config = mxs::config;

namespace csmon
{

config::Specification specification(MXS_MODULE_NAME, config::Specification::MONITOR);

config::ParamServer primary(
    &specification,
    "primary",
    "For pre-1.2 Columnstore servers, specifies which server is chosen as the master.",
    config::Param::OPTIONAL);

config::ParamCount admin_port(
    &specification,
    "admin_port",
    "Port of the Columnstore administrative daemon.");
}
}

CsConfig::CsConfig(const std::string& name)
    : mxs::config::Configuration(name, &csmon::specification)
{
    add_native(&this->pPrimary, &csmon::primary);
    add_native(&this->admin_port, &csmon::admin_port);
}

//static
void CsConfig::populate(MXS_MODULE& info)
{
    csmon::specification.populate(info);
}

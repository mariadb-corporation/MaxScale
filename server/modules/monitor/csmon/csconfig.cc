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

config::ParamString admin_base_path(
    &specification,
    "admin_base_path",
    "The base path to be used when accessing the Columnstore administrative daemon. "
    "If, for instance, a daemon URL is https://localhost:8640/cmapi/0.3.0/node/start "
    "then the admin_base_path is \"/cmapi/0.3.0\".");

config::ParamString api_key(
    &specification,
    "api_key",
    "The API key to be used in the communication with the Columnstora admin daemon.");

}


CsConfig::CsConfig(const std::string& name)
    : mxs::config::Configuration(name, &csmon::specification)
{
    add_native(&this->pPrimary, &csmon::primary);
    add_native(&this->admin_port, &csmon::admin_port);
    add_native(&this->admin_base_path, &csmon::admin_base_path);
    add_native(&this->api_key, &csmon::api_key);
}

//static
void CsConfig::populate(MXS_MODULE& info)
{
    csmon::specification.populate(info);
}

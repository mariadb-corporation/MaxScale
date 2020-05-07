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

const config::ParamCount::value_type  DEFAULT_ADMIN_PORT      = 8630;
const config::ParamString::value_type DEFAULT_ADMIN_BASE_PATH = "/cmapi/0.3.0";
const config::ParamString::value_type DEFAULT_API_KEY         = "";
const config::ParamServer::value_type DEFAULT_PRIMARY         = nullptr;

config::Specification specification(MXS_MODULE_NAME, config::Specification::MONITOR);

config::ParamEnum<cs::Version> version(
    &specification,
    "version",
    "The version of the Columnstore cluster that is monitored. Default is 'CS_15'.",
    {
        { cs::CS_10, "CS_10" },
        { cs::CS_12, "CS_12" },
        { cs::CS_15, "CS_15" }
    },
    cs::CS_15);

config::ParamServer primary(
    &specification,
    "primary",
    "For pre-1.2 Columnstore servers, specifies which server is chosen as the master.",
    config::Param::OPTIONAL);

config::ParamCount admin_port(
    &specification,
    "admin_port",
    "Port of the Columnstore administrative daemon.",
    DEFAULT_ADMIN_PORT);

config::ParamString admin_base_path(
    &specification,
    "admin_base_path",
    "The base path to be used when accessing the Columnstore administrative daemon. "
    "If, for instance, a daemon URL is https://localhost:8640/cmapi/0.3.0/node/start "
    "then the admin_base_path is \"/cmapi/0.3.0\".",
    DEFAULT_ADMIN_BASE_PATH);

config::ParamString api_key(
    &specification,
    "api_key",
    "The API key to be used in the communication with the Columnstora admin daemon.",
    DEFAULT_API_KEY);
}


CsConfig::CsConfig(const std::string& name)
    : mxs::config::Configuration(name, &csmon::specification)
{
    add_native(&this->version, &csmon::version);
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

namespace
{

void complain_invalid(cs::Version version, const std::string& param)
{
    MXS_ERROR("When csmon is configured for Columnstore %s, "
              "the parameter '%s' is invalid.",
              cs::to_string(version), param.c_str());
}

void complain_mandatory(cs::Version version, const std::string& param)
{
    MXS_ERROR("When csmon is configured for Columnstore %s, "
              "the parameter '%s' is mandatory.",
              cs::to_string(version), param.c_str());
}

}

bool CsConfig::post_configure()
{
    bool rv = true;

    if (!check_mandatory())
    {
        rv = false;
    }

    if (!check_invalid())
    {
        rv = false;
    }

    return rv;
}

bool CsConfig::check_mandatory()
{
    bool rv = true;

    switch (this->version)
    {
    case cs::CS_10:
        if (this->pPrimary == csmon::DEFAULT_PRIMARY)
        {
            complain_mandatory(this->version, csmon::primary.name());
            rv = false;
        }
        break;

    case cs::CS_12:
        break;

    case cs::CS_15:
        if (this->api_key == csmon::DEFAULT_API_KEY)
        {
            complain_mandatory(this->version, csmon::api_key.name());
            rv = false;
        }
        break;

    case cs::CS_UNKNOWN:
        mxb_assert(!true);
    }

    return rv;
}

bool CsConfig::check_invalid()
{
    bool rv = true;

    switch (this->version)
    {
    case cs::CS_12:
        if (this->pPrimary != csmon::DEFAULT_PRIMARY)
        {
            complain_invalid(this->version, csmon::primary.name());
            rv = false;
        }
        // Flow through intended.
    case cs::CS_10:
        // If any of the 1.5 parameters are different from their default, we assume
        // they have been set.
        // TODO: Modify config2 so that you can ask whether a value has been explicitly set.

        if (this->admin_port != csmon::DEFAULT_ADMIN_PORT)
        {
            complain_invalid(this->version, csmon::admin_port.name());
            rv = false;
        }

        if (this->admin_base_path != csmon::DEFAULT_ADMIN_BASE_PATH)
        {
            complain_invalid(this->version, csmon::admin_base_path.name());
            rv = false;
        }

        if (this->api_key != csmon::DEFAULT_API_KEY)
        {
            complain_invalid(this->version, csmon::api_key.name());
            rv = false;
        }
        break;

    case cs::CS_15:
        if (this->pPrimary != csmon::DEFAULT_PRIMARY)
        {
            complain_invalid(this->version, csmon::primary.name());
            rv = false;
        }
        break;

    case cs::CS_UNKNOWN:
        mxb_assert(!true);
        rv = false;
    }

    return rv;
}


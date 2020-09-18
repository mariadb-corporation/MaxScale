/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "csconfig.hh"
#include <sys/stat.h>
#include <fstream>
#include <random>
#include <maxscale/paths.hh>

namespace config = mxs::config;
using namespace std;

namespace
{

string get_random_string(int length)
{
    mt19937 generator { random_device{}() };

    uniform_int_distribution<int> distribution{'a', 'z'};

    string s(length, '\0');
    for (auto& c : s)
    {
        c = distribution(generator);
    }

    return s;
}
}

namespace csmon
{

const char ZAPI_KEY_FILE_NAME[] = "api_key.txt";

using seconds = chrono::seconds;

using ClusterMonitorInterval = config::ParamDuration<std::chrono::milliseconds>;

const config::ParamCount::value_type     DEFAULT_ADMIN_PORT               = 8640;
const config::ParamString::value_type    DEFAULT_ADMIN_BASE_PATH          = "/cmapi/0.4.0";
const config::ParamString::value_type    DEFAULT_API_KEY                  = "";
const config::ParamString::value_type    DEFAULT_LOCAL_ADDRESS            = "";
const config::ParamServer::value_type    DEFAULT_PRIMARY                  = nullptr;
const config::ParamBool::value_type      DEFAULT_DYNAMIC_NODE_DETECTION   = false;
const ClusterMonitorInterval::value_type DEFAULT_CLUSTER_MONITOR_INTERVAL = std::chrono::milliseconds(10000);

config::Specification specification(MXS_MODULE_NAME, config::Specification::MONITOR);

config::ParamEnum<cs::Version> version(
    &specification,
    "version",
    "The version of the Columnstore cluster that is monitored. Default is '1.5'.",
    {
        { cs::CS_15, cs::ZCS_15 }
    },
    cs::CS_15);

config::ParamCount admin_port(
    &specification,
    "admin_port",
    "Port of the Columnstore administrative daemon.",
    DEFAULT_ADMIN_PORT);

config::ParamString admin_base_path(
    &specification,
    "admin_base_path",
    "The base path to be used when accessing the Columnstore administrative daemon. "
    "If, for instance, a daemon URL is https://localhost:8640/cmapi/0.4.0/node/start "
    "then the admin_base_path is \"/cmapi/0.4.0\".",
    DEFAULT_ADMIN_BASE_PATH);

config::ParamString api_key(
    &specification,
    "api_key",
    "The API key to be used in the communication with the Columnstora admin daemon.",
    DEFAULT_API_KEY);

config::ParamString local_address(
    &specification,
    "local_address",
    "Local address to provide as IP of MaxScale to Columnstore cluster. Need not be "
    "specified if global 'local_address' has been set.",
    DEFAULT_LOCAL_ADDRESS);

config::ParamBool dynamic_node_detection(
    &specification,
    "dynamic_node_detection",
    "Should cluster configuration be figured out at runtime.",
    DEFAULT_DYNAMIC_NODE_DETECTION);

config::ParamDuration<std::chrono::milliseconds> cluster_monitor_interval(
    &specification,
    "cluster_monitor_interval",
    "With what interval the cluster configuration should be probed.",
    mxs::config::INTERPRET_AS_MILLISECONDS,
    DEFAULT_CLUSTER_MONITOR_INTERVAL);

}

CsConfig::CsConfig(const string& name)
    : mxs::config::Configuration(name, &csmon::specification)
{
    add_native(&CsConfig::version, &csmon::version);
    add_native(&CsConfig::admin_port, &csmon::admin_port);
    add_native(&CsConfig::admin_base_path, &csmon::admin_base_path);
    add_native(&CsConfig::api_key, &csmon::api_key);
    add_native(&CsConfig::local_address, &csmon::local_address);
    add_native(&CsConfig::dynamic_node_detection, &csmon::dynamic_node_detection);
    add_native(&CsConfig::cluster_monitor_interval, &csmon::cluster_monitor_interval);
}

//static
void CsConfig::populate(MXS_MODULE& info)
{
    info.specification = &csmon::specification;
}

namespace
{

void complain_invalid(cs::Version version, const string& param)
{
    MXS_ERROR("When csmon is configured for Columnstore %s, "
              "the parameter '%s' is invalid.",
              cs::to_string(version), param.c_str());
}

void complain_mandatory(cs::Version version, const string& param)
{
    MXS_ERROR("When csmon is configured for Columnstore %s, "
              "the parameter '%s' is mandatory.",
              cs::to_string(version), param.c_str());
}

}

bool CsConfig::post_configure()
{
    bool rv = true;

    string path { mxs::datadir() };
    path += "/";
    path += name();

    // We do not bail out at first error, better to complain as much as we can.

    if (mxs_mkdir_all(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP))
    {
        if (!check_api_key(path))
        {
            rv = false;
        }
    }
    else
    {
        MXS_ERROR("Could not access or create directory '%s'.", path.c_str());
        rv = false;
    }

    if (!check_mandatory())
    {
        rv = false;
    }

    return rv;
}

namespace
{

string read_api_key(const string& path)
{
    string key;

    ifstream in(path);

    if (in)
    {
        in >> key;
    }
    else
    {
        MXS_NOTICE("Could not open '%s', no api key yet stored.", path.c_str());
    }

    return key;
}

bool write_api_key(const string& path, const string& key)
{
    bool rv = false;

    ofstream out(path, ios_base::out | ios_base::trunc);

    if (out)
    {
        out << key << endl;

        if (out.bad())
        {
            MXS_ERROR("Could not write new api key to '%s'.", path.c_str());
        }
        else
        {
            MXS_NOTICE("Stored new api key in '%s'.", path.c_str());
            rv = true;
        }
    }
    else
    {
        MXS_ERROR("Could not open '%s' for writing, the Columnstore api key can not be stored.",
                  path.c_str());
    }

    return rv;
}
}

bool CsConfig::check_api_key(const string& dir)
{
    bool rv = true;

    if (this->version == cs::CS_15)
    {
        string path = dir;
        path += "/";
        path += csmon::ZAPI_KEY_FILE_NAME;

        string stored_key = read_api_key(path);

        if (this->api_key.empty())
        {
            if (stored_key.empty())
            {
                MXS_NOTICE("No api-key specified and no stored api-key found, generating one.");

                string new_key = get_random_string(16);

                new_key = "maxscale-" + new_key;
                this->api_key = new_key;
            }
            else
            {
                MXS_NOTICE("Using api-key from '%s'.", path.c_str());
                this->api_key = stored_key;
            }
        }

        if (this->api_key != stored_key)
        {
            MXS_NOTICE("Specified api key is different from stored one, storing the specified one.");
            rv = write_api_key(path, this->api_key);
        }
    }

    return rv;
}

bool CsConfig::check_mandatory()
{
    bool rv = true;

    switch (this->version)
    {
    case cs::CS_15:
        if (this->api_key == csmon::DEFAULT_API_KEY)
        {
            complain_mandatory(this->version, csmon::api_key.name());
            rv = false;
        }

        if (this->local_address == csmon::DEFAULT_LOCAL_ADDRESS)
        {
            string local_address = mxs::Config::get().local_address;

            if (!local_address.empty())
            {
                this->local_address = local_address;
            }
            else
            {
                MXS_ERROR("'local_address' has been specified neither for %s, nor globally.",
                          name().c_str());
                rv = false;
            }
        }
        break;

    case cs::CS_UNKNOWN:
        mxb_assert(!true);
    }

    return rv;
}

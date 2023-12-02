/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
#include "redisconfig.hh"
#include <maxscale/cn_strings.hh>

namespace config = mxs::config;

using namespace std;

namespace
{

class Specification : public config::Specification
{
public:
    using config::Specification::Specification;

private:
    template<class Params>
    bool do_post_validate(Params& params) const;

    bool post_validate(const config::Configuration* pConfig,
                       const mxs::ConfigParameters& params,
                       const map<string, mxs::ConfigParameters>& nested_params) const override final
    {
        return do_post_validate(params);
    }

    bool post_validate(const config::Configuration* pConfig,
                       json_t* pJson,
                       const map<string, json_t*>& nested_params) const override final
    {
        return do_post_validate(pJson);
    }
};

namespace storage_redis
{

config::Specification specification(MXB_MODULE_NAME, config::Specification::FILTER);

config::ParamHost host(
    &specification,
    "server",
    "The Redis server host. Must be of the format 'address[:port]'",
    mxb::Host(RedisConfig::DEFAULT_ADDRESS, RedisConfig::DEFAULT_PORT),
    RedisConfig::DEFAULT_PORT);

config::ParamString username(
    &specification,
    "username",
    "The username to use when authenticating to Redis.",
    "");

config::ParamString password(
    &specification,
    "password",
    "The password to use when authenticating to Redis.",
    "");

config::ParamBool ssl(
    &specification,
    CN_SSL,
    "Enable TLS for server",
    false);

config::ParamPath ssl_cert(
    &specification,
    CN_SSL_CERT,
    "TLS public certificate",
    config::ParamPath::R,
    "");

config::ParamPath ssl_key(
    &specification,
    CN_SSL_KEY, "TLS private key",
    config::ParamPath::R,
    "");

config::ParamPath ssl_ca(
    &specification,
    CN_SSL_CA,
    "TLS certificate authority",
    config::ParamPath::R,
    "");

}

template<class Params>
bool Specification::do_post_validate(Params& params) const
{
    bool rv = true;

    string username = storage_redis::username.get(params);
    string password = storage_redis::password.get(params);

    if (!username.empty() && password.empty())
    {
        MXB_ERROR("If '%s' is provided, then '%s' must be provided.",
                  storage_redis::username.name().c_str(),
                  storage_redis::password.name().c_str());
        rv = false;
    }

    return rv;
}

}


RedisConfig::RedisConfig(const string& name)
    : config::Configuration(name, &storage_redis::specification)
{
    add_native(&RedisConfig::host, &storage_redis::host);
    add_native(&RedisConfig::username, &storage_redis::username);
    add_native(&RedisConfig::password, &storage_redis::password);
    add_native(&RedisConfig::ssl, &storage_redis::ssl);
    add_native(&RedisConfig::ssl_key, &storage_redis::ssl_key);
    add_native(&RedisConfig::ssl_cert, &storage_redis::ssl_cert);
    add_native(&RedisConfig::ssl_ca, &storage_redis::ssl_ca);
}

//static
const mxs::config::Specification& RedisConfig::specification()
{
    return storage_redis::specification;
}

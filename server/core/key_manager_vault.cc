/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/key_manager_vault.hh"
#include <maxbase/json.hh>
#include <maxscale/utils.hh>

#include <libvault/VaultClient.h>

namespace
{

using Opt = mxs::config::ParamPath::Options;

static mxs::config::Specification s_spec("key_manager_vault", mxs::config::Specification::GLOBAL);

static mxs::config::ParamPassword s_token(&s_spec, "token", "Authentication token");
static mxs::config::ParamString s_host(&s_spec, "host", "Vault server host", "localhost");
static mxs::config::ParamInteger s_port(&s_spec, "port", "Vault server port", 8200);
static mxs::config::ParamPath s_ca(&s_spec, "ca", "CA certificate", Opt::R, "");
static mxs::config::ParamString s_mount(&s_spec, "mount", "KeyValue mount", "secret");
static mxs::config::ParamBool s_tls(&s_spec, "tls", "Use HTTPS with Vault server", true);
static mxs::config::ParamSeconds s_timeout(&s_spec, "timeout", "Timeout for the Vault server", 30s);

std::tuple<std::vector<uint8_t>, uint32_t>
load_key(const VaultKey::Config& cnf, const std::string& id, int64_t version)
{
    uint32_t key_version = 0;
    std::vector<uint8_t> rval;
    bool err = false;

    auto error_cb = [&](std::string msg){
        MXB_SERROR("Vault error: " << msg);
        err = true;
    };

    auto http_error_cb = [&](Vault::HttpResponse resp) {
        if (resp.statusCode.value() == 404)
        {
            MXB_SERROR("Could not find key '/" << cnf.mount << "/" << id << "'");
        }
        else
        {
            MXB_SERROR("Vault HTTP error: " << resp.statusCode << " " << resp.body);
        }
        err = true;
    };

    Vault::TokenStrategy auth{Vault::Token{cnf.token}};

    auto builder = Vault::ConfigBuilder()
        .withTlsEnabled(cnf.tls)
        .withPort(Vault::Port {std::to_string(cnf.port)})
        .withHost(Vault::Host {cnf.host})
        .withConnectTimeout(Vault::Timeout {cnf.timeout.count()})
        .withRequestTimeout(Vault::Timeout {cnf.timeout.count()});

    if (!cnf.ca.empty())
    {
        builder.withCaBundle(cnf.ca);
    }

    Vault::Config config = builder.build();
    Vault::Client client{config, auth, error_cb, http_error_cb};
    Vault::SecretMount mount{cnf.mount};
    Vault::KeyValue kv{client, mount};
    Vault::Path key{id};
    Vault::SecretVersion secret_version{version};

    if (auto response = version == 0 ? kv.read(key) : kv.read(key, secret_version))
    {
        mxb::Json js;
        MXB_AT_DEBUG(bool ok = ) js.load_string(response.value());
        mxb_assert(ok);

        if (auto data = js.at("/data/data/data"))
        {
            rval = mxs::from_hex(mxb::trimmed_copy(data.get_string()));

            if (rval.empty())
            {
                MXB_ERROR("Key 'data' for secret '%s' was not a hex-encoded encryption key.", id.c_str());
            }
            else
            {
                if (auto ver = js.at("/data/metadata/version"); ver.type() == mxb::Json::Type::INTEGER)
                {
                    key_version = ver.get_int();
                }
                else
                {
                    rval.clear();
                    MXB_ERROR("Failed to retrieve version of secret '%s'.", id.c_str());
                }
            }
        }
        else
        {
            MXB_ERROR("Key 'data' was not found for secret '%s'.", id.c_str());
        }
    }
    else if (!err)
    {
        MXB_ERROR("Could not find secret '%s'.", id.c_str());
    }

    return {rval, key_version};
}
}

// static
mxs::config::Specification* VaultKey::specification()
{
    return &s_spec;
}

// static
std::unique_ptr<mxs::KeyManager::MasterKey> VaultKey::create(const mxs::ConfigParameters& params)
{
    VaultKey::Config config;
    std::unique_ptr<VaultKey> rv;

    if (s_spec.validate(params) && config.configure(params))
    {
        rv = std::make_unique<VaultKey>(std::move(config));
    }

    return rv;
}

std::tuple<bool, uint32_t, std::vector<uint8_t>>
VaultKey::get_key(const std::string& id, uint32_t version) const
{
    auto [key, keyversion] = load_key(m_config, id, version);
    return {!key.empty(), keyversion, key};
}

VaultKey::VaultKey(Config config)
    : m_config(std::move(config))
{
}

VaultKey::Config::Config()
    : mxs::config::Configuration("key_manager_vault", &s_spec)
{
    add_native(&Config::token, &s_token);
    add_native(&Config::host, &s_host);
    add_native(&Config::port, &s_port);
    add_native(&Config::ca, &s_ca);
    add_native(&Config::mount, &s_mount);
    add_native(&Config::tls, &s_tls);
    add_native(&Config::timeout, &s_timeout);
}

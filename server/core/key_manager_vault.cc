/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-05-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/key_manager_vault.hh"
#include <maxbase/json.hh>

#include <libvault/VaultClient.h>

namespace
{

using Opt = mxs::config::ParamPath::Options;

static mxs::config::Specification s_spec("key_manager_vault", mxs::config::Specification::GLOBAL);

static mxs::config::ParamString s_token(&s_spec, "token", "Authentication token");
static mxs::config::ParamString s_id(&s_spec, "id", "Key ID");
static mxs::config::ParamString s_host(&s_spec, "host", "Vault server host", "localhost");
static mxs::config::ParamInteger s_port(&s_spec, "port", "Vault server port", 8200);
static mxs::config::ParamPath s_ca(&s_spec, "ca", "CA certificate", Opt::R, "");
static mxs::config::ParamString s_mount(&s_spec, "mount", "KeyValue mount", "secret");
static mxs::config::ParamBool s_tls(&s_spec, "tls", "Use HTTPS with Vault server", true);
static mxs::config::ParamCount s_version(&s_spec, "version", "Vault secret version", 0);

std::vector<uint8_t> load_key(const VaultKey::Config& cnf)
{
    std::vector<uint8_t> rval;
    bool err = false;

    auto error_cb = [&](std::string msg){
        MXB_SERROR("Vault error: " << msg);
        err = true;
    };

    auto http_error_cb = [&](Vault::HttpResponse resp) {
        if (resp.statusCode.value() == 404)
        {
            MXB_SERROR("Could not find key '/" << cnf.mount << "/" << cnf.id << "'");
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
        .withHost(Vault::Host {cnf.host});

    if (!cnf.ca.empty())
    {
        builder.withCaBundle(cnf.ca);
    }

    Vault::Config config = builder.build();
    Vault::Client client{config, auth, error_cb, http_error_cb};
    Vault::SecretMount mount{cnf.mount};
    Vault::KeyValue kv{client, mount};
    Vault::Path key{cnf.id};
    Vault::SecretVersion version{cnf.version};

    if (auto response = cnf.version == 0 ? kv.read(key) : kv.read(key, version))
    {
        mxb::Json js;
        MXB_AT_DEBUG(bool ok = ) js.load_string(response.value());
        mxb_assert(ok);
        const char* path = "/data/data/data";

        if (auto data = js.at(path))
        {
            rval = mxs::from_hex(mxb::trimmed_copy(data.get_string()));

            if (rval.empty())
            {
                MXB_ERROR("Key 'data' for secret '%s' was not a hex-encoded encryption key.", cnf.id.c_str());
            }
        }
        else
        {
            MXB_ERROR("Key 'data' was not found for secret '%s'.", cnf.id.c_str());
        }
    }
    else if (!err)
    {
        MXB_ERROR("Could not find secret '%s'.", cnf.id.c_str());
    }

    return rval;
}
}

// static
std::unique_ptr<mxs::KeyManager::MasterKey> VaultKey::create(const mxs::ConfigParameters& params)
{
    VaultKey::Config config;
    std::unique_ptr<VaultKey> rv;

    if (s_spec.validate(params) && config.configure(params))
    {
        auto key = load_key(config);

        if (!key.empty())
        {
            rv = std::make_unique<VaultKey>(std::move(config), std::move(key));
        }
    }

    return rv;
}

VaultKey::VaultKey(Config config, std::vector<uint8_t> key)
    : mxs::KeyManager::MasterKeyBase(std::move(key))
    , m_config(std::move(config))
{
}

VaultKey::Config::Config()
    : mxs::config::Configuration("key_manager_vault", &s_spec)
{
    add_native(&Config::token, &s_token);
    add_native(&Config::id, &s_id);
    add_native(&Config::host, &s_host);
    add_native(&Config::port, &s_port);
    add_native(&Config::ca, &s_ca);
    add_native(&Config::mount, &s_mount);
    add_native(&Config::tls, &s_tls);
    add_native(&Config::version, &s_version);
}

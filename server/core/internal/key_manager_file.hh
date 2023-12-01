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
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/key_manager.hh>
#include <maxscale/config2.hh>

namespace cfg
{
using Opt = mxs::config::ParamPath::Options;

static mxs::config::Specification spec {"key_manager_file", mxs::config::Specification::GLOBAL};
static mxs::config::ParamPath keyfile {&cfg::spec, "keyfile", "Path to the encryption key", Opt::R};
}

class FileKey : public mxs::KeyManager::MasterKey
{
public:

    static mxs::config::Specification* specification()
    {
        return &cfg::spec;
    }

    static std::unique_ptr<mxs::KeyManager::MasterKey> create(const mxs::ConfigParameters& options)
    {
        Config config;
        std::unique_ptr<mxs::KeyManager::MasterKey> rv;

        if (config.specification().validate(options) && config.configure(options))
        {
            if (auto key = load_key_file(config); !key.empty())
            {
                rv.reset(new FileKey(std::move(key)));
            }
        }

        return rv;
    }

    std::tuple<bool, uint32_t, std::vector<uint8_t>>
    get_key(const std::string& id, uint32_t version) const override final
    {
        std::vector<uint8_t> key;
        auto it = m_keys.find(id);

        if (it != m_keys.end())
        {
            key = it->second;
        }

        return {it != m_keys.end(), MasterKey::NO_VERSIONING, key};
    }

private:

    class Config : public mxs::config::Configuration
    {
    public:
        Config()
            : mxs::config::Configuration(cfg::spec.module(), &cfg::spec)
        {
            add_native(&Config::keyfile, &cfg::keyfile);
        }

        std::string keyfile;
    };

    FileKey(std::map<std::string, std::vector<uint8_t>> keys)
        : m_keys(std::move(keys))
    {
    }

    static bool is_hex_key(const std::string& key)
    {
        // Accept hex keys of 16 bytes or more that are a power of two.
        size_t s = key.size();
        return s > 32 && (s & (s - 1)) == 0;
    }

    static std::map<std::string, std::vector<uint8_t>> load_key_file(const Config& config)
    {
        std::map<std::string, std::vector<uint8_t>> rval;
        auto [str, err] = mxb::load_file<std::string>(config.keyfile);

        if (!str.empty())
        {
            bool error = false;
            mxb::trim(str);

            for (auto line : mxb::strtok(str, "\n"))
            {
                mxb::trim(line);

                if (auto tok = mxb::strtok(line, ";"); tok.size() == 2)
                {
                    mxb::trim(tok[0]);
                    mxb::trim(tok[1]);
                    char* end;

                    if (tok[0].empty() || strtol(tok[0].c_str(), &end, 10) <= 0 || *end != '\0')
                    {
                        MXB_ERROR("Key ID is not a number.");
                        error = true;
                    }
                    else if (!is_hex_key(tok[1]))
                    {
                        MXB_ERROR("Invalid key size for encryption key '%s'.", tok[0].c_str());
                        error = true;
                    }
                    else if (auto key = mxs::from_hex(tok[1]); key.empty())
                    {
                        MXB_ERROR("Invalid hexadecimal data in encryption key.");
                        error = true;
                    }
                    else
                    {
                        rval.emplace(std::move(tok[0]), std::move(key));
                    }
                }
                else
                {
                    MXB_ERROR("Found incorrectly formatted row.");
                    error = true;
                }
            }

            if (error)
            {
                MXB_ERROR("File '%s' does not contain a valid encryption key.", config.keyfile.c_str());
                rval.clear();
            }
        }
        else if (!err.empty())
        {
            MXB_ERROR("%s", err.c_str());
        }

        return rval;
    }

    std::map<std::string, std::vector<uint8_t>> m_keys;
};

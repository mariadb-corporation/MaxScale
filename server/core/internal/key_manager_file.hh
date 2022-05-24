/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/key_manager.hh>

class FileKey : public mxs::KeyManager::MasterKeyBase
{
public:
    static constexpr const char CN_KEYFILE[] = "keyfile";

    static std::unique_ptr<mxs::KeyManager::MasterKey> create(const mxs::ConfigParameters& options)
    {
        std::unique_ptr<mxs::KeyManager::MasterKey> rv;

        if (auto keyfile = options.get_string(CN_KEYFILE); !keyfile.empty())
        {
            if (auto key = load_hex_file(keyfile); !key.empty())
            {
                switch (key.size())
                {
                case 16:
                case 24:
                case 32:
                    rv.reset(new FileKey(std::move(key)));
                    break;

                default:
                    MXB_ERROR("Invalid key size (%ld bytes), expected 16, 24 or 32 bytes.", key.size());
                    break;
                }
            }
            else
            {
                MXB_ERROR("Failed to open keyfile '%s'.", keyfile.c_str());
            }
        }
        else
        {
            MXB_ERROR("Missing required '%s' parameter.", CN_KEYFILE);
        }

        return rv;
    }

private:
    using MasterKeyBase::MasterKeyBase;

    static std::vector<uint8_t> load_hex_file(std::string file)
    {
        std::vector<uint8_t> rval;
        auto [str, err] = mxb::load_file<std::string>(file);

        if (!str.empty())
        {
            mxb::trim(str);
            rval = mxs::from_hex(str);

            if (rval.empty())
            {
                MXB_ERROR("File '%s' does not contain a valid hex string.", file.c_str());
            }
        }
        else if (!err.empty())
        {
            MXB_ERROR("%s", err.c_str());
        }

        return rval;
    }
};

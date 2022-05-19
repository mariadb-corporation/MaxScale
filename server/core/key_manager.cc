/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#include <maxscale/key_manager.hh>

#include <maxbase/json.hh>
#include <maxbase/secrets.hh>
#include <maxscale/paths.hh>
#include <maxscale/utils.hh>
#include <maxscale/config.hh>

#include <fstream>
#include <unistd.h>

namespace
{
struct ThisUnit
{
    std::unique_ptr<mxs::KeyManager> manager;
};

ThisUnit this_unit;

std::vector<uint8_t> load_file(std::string file)
{
    std::vector<uint8_t> data;
    std::ifstream infile(file, std::ios_base::ate);

    if (infile)
    {
        data.resize(infile.tellg());
        infile.seekg(0, std::ios_base::beg);

        if (!infile.read(reinterpret_cast<char*>(data.data()), data.size()))
        {
            MXB_ERROR("Failed to read from file '%s': %d, %s", file.c_str(), errno, mxb_strerror(errno));
            data.clear();
        }
    }
    else if (errno != ENOENT)
    {
        MXB_ERROR("Failed to open file '%s': %d, %s", file.c_str(), errno, mxb_strerror(errno));
    }

    return data;
}

bool save_file(std::string file, std::vector<uint8_t> data)
{
    bool ok = false;
    std::string tmp = file + ".tmp";
    std::ofstream outfile(tmp);

    if (outfile && outfile.write(reinterpret_cast<char*>(data.data()), data.size()))
    {
        outfile.close();

        if (rename(tmp.c_str(), file.c_str()) == 0)
        {
            ok = true;
        }
        else
        {
            MXB_ERROR("Failed to rename '%s' to '%s': %d, %s",
                      tmp.c_str(), file.c_str(), errno, mxb_strerror(errno));
        }
    }
    else
    {
        MXB_ERROR("Write to file '%s' failed: %d, %s", tmp.c_str(), errno, mxb_strerror(errno));
    }

    return ok;
}
}

namespace maxscale
{

// static
bool KeyManager::init()
{
    Type type = Type::NONE;
    mxs::ConfigParameters opts;

    if (type == Type::NONE)
    {
        return true;
    }

    auto keystore = mxs::datadir() + "/keystore"s;

    std::unique_ptr<MasterKey> master_key;

    switch (type)
    {
    default:
        mxb_assert(!true);
        break;
    }

    bool ok = false;
    std::unique_ptr<KeyManager> rv;

    if (master_key)
    {
        rv.reset(new KeyManager(std::move(master_key), std::move(keystore)));

        if (rv->load_keys())
        {
            ok = true;
            this_unit.manager = std::move(rv);
        }
    }

    return ok;
}

KeyManager* key_manager()
{
    return this_unit.manager.get();
}

KeyManager::KeyManager(std::unique_ptr<MasterKey> master_key, std::string keystore)
    : m_master_key(std::move(master_key))
    , m_keystore(std::move(keystore))
{
}

std::tuple<bool, uint32_t, std::vector<uint8_t>> KeyManager::latest_key(const std::string& id)
{
    std::lock_guard guard(m_lock);
    bool ok = false;
    auto& keymap = m_keys[id];

    if (!keymap.empty() || rotate_key(keymap))
    {
        auto it = keymap.rbegin();
        return {true, it->first, it->second};
    }

    return {false, 0, {}};
}

std::pair<bool, std::vector<uint8_t>> KeyManager::key(const std::string& id, uint32_t version)
{
    std::lock_guard guard(m_lock);

    if (auto it = m_keys.find(id); it != m_keys.end())
    {
        if (auto it2 = it->second.find(version); it2 != it->second.end())
        {
            return {true, it2->second};
        }
    }
    return {false, {}};
}

bool KeyManager::rotate_key(KeyMap& keymap)
{
    bool ok = false;

    // Currently hard-coded to always generate 256-bit keys. The AES mode itself doesn't matter in this case
    // as we just want a key of a certain length.
    // TODO: Make new_key() a static function?
    auto key = mxb::Cipher(mxb::Cipher::AES_CBC, 256).new_key();

    if (!key.empty())
    {
        uint32_t version = keymap.empty() ? 0 : keymap.rbegin()->first;
        MXB_AT_DEBUG(auto inserted = ) keymap.emplace(version + 1, std::move(key));
        mxb_assert(inserted.second);

        ok = save_keys();
    }

    return ok;
}

bool KeyManager::rotate(const std::string& id)
{
    std::lock_guard guard(m_lock);
    return rotate_key(m_keys[id]);
}

bool KeyManager::load_keys()
{
    bool ok = false;
    auto encrypted = load_file(m_keystore);

    if (!encrypted.empty())
    {
        auto [decrypt_ok, plaintext] = m_master_key->decrypt(encrypted);

        if (decrypt_ok)
        {
            mxb::Json js;
            std::string str(plaintext.begin(), plaintext.end());

            if (js.load_string(str))
            {
                ok = true;

                for (const auto& id : js.keys())
                {
                    for (const auto& e : js.get_array_elems(id))
                    {
                        uint32_t version = e.get_int("version");
                        auto key = mxs::from_base64(e.get_string("key"));
                        m_keys[id][version] = std::move(key);
                    }
                }
            }
        }
    }

    return ok;
}


bool KeyManager::save_keys()
{
    mxb::Json js(mxb::Json::Type::OBJECT);

    for (const auto& [id, key] : m_keys)
    {
        mxb::Json arr(mxb::Json::Type::ARRAY);

        for (const auto& [version, data] : key)
        {
            mxb::Json value(mxb::Json::Type::OBJECT);
            value.set_int("version", version);
            value.set_string("key", mxs::to_base64(data));
            arr.add_array_elem(std::move(value));
        }

        js.set_object(id.c_str(), std::move(arr));
    }

    std::string data = js.to_string(mxb::Json::Format::COMPACT);
    auto [ok, ciphertext] = m_master_key->encrypt({data.begin(), data.end()});

    if (ok)
    {
        ok = save_file(m_keystore, ciphertext);
    }

    return ok;
}
}

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
#include <maxbase/filesystem.hh>
#include <maxbase/secrets.hh>
#include <maxscale/paths.hh>
#include <maxscale/utils.hh>
#include <maxscale/config.hh>

#include <fstream>
#include <unistd.h>

// Always build the file key manager
#include "internal/key_manager_file.hh"

#ifdef BUILD_KMIP_KEY_MANAGER
#include "internal/key_manager_kmip.hh"
#endif

namespace
{
struct ThisUnit
{
    std::unique_ptr<mxs::KeyManager> manager;
};

ThisUnit this_unit;
}

namespace maxscale
{

std::pair<bool, std::vector<uint8_t>>
KeyManager::MasterKeyBase::decrypt(std::vector<uint8_t> input)
{
    bool ok = false;
    std::vector<uint8_t> output;
    size_t iv_len = m_cipher.iv_size();

    if (input.size() >= iv_len + m_cipher.encrypted_size(1))
    {
        output.resize(input.size() - iv_len);

        int out_len = 0;
        ok = m_cipher.decrypt(m_key.data(), input.data(),
                              input.data() + iv_len, input.size() - iv_len,
                              output.data(), &out_len);

        if (!ok)
        {
            auto err = m_cipher.get_errors();

            if (err.empty())
            {
                // If decrypt() fails but no error is stored, we know GCM verification failed.
                err = "Data verification failure";
            }

            MXB_ERROR("Decryption failure: %s", err.c_str());
        }

        output.resize(out_len);
    }
    else
    {
        MXB_ERROR("Cannot decrypt: input too small");
    }

    return {ok, output};
}

std::pair<bool, std::vector<uint8_t>>
KeyManager::MasterKeyBase::encrypt(std::vector<uint8_t> input)
{
    auto output = m_cipher.new_iv();
    size_t iv_size = m_cipher.iv_size();
    mxb_assert(output.size() == iv_size);

    // Append the encrypted data to the IV
    output.resize(m_cipher.encrypted_size(input.size()) + iv_size);

    int out_len = 0;
    bool ok = m_cipher.encrypt(m_key.data(), output.data(),
                               input.data(), input.size(),
                               output.data() + iv_size, &out_len);

    if (!ok)
    {
        MXB_ERROR("Encryption failure: %s", m_cipher.get_errors().c_str());
    }

    // The resulting size should be the same as the one we pre-calculated.
    mxb_assert((size_t)out_len == output.size() - iv_size);

    return {ok, output};
}

KeyManager::MasterKeyBase::MasterKeyBase(std::vector<uint8_t> key)
    : m_key(std::move(key))
    , m_cipher(mxb::Cipher::AES_GCM, m_key.size() * 8)
{
}


// static
bool KeyManager::init()
{
    const auto& cnf = mxs::Config::get();
    Type type = cnf.key_manager;
    mxs::ConfigParameters opts;

    if (type == Type::NONE)
    {
        return true;
    }

    for (std::string tok : mxb::strtok(cnf.key_manager_options, ","))
    {
        auto pos = tok.find('=');

        if (pos == std::string::npos)
        {
            MXB_ERROR("Invalid option string value: %s", tok.c_str());
            return false;
        }

        opts.set(mxb::trimmed_copy(tok.substr(0, pos)), mxb::trimmed_copy(tok.substr(pos + 1)));
    }

    auto keystore = opts.get_string("keystore");

    if (keystore.empty())
    {
        keystore = mxs::datadir() + "/keystore"s;
    }

    std::unique_ptr<MasterKey> master_key;

    switch (type)
    {
    case Type::FILE:
        master_key = FileKey::create(opts);
        break;

    case Type::KMIP:
#ifdef BUILD_KMIP_KEY_MANAGER
        master_key = KMIPKey::create(opts);
#else
        MXB_ERROR("KMIP key manager is not included in this MaxScale installation.");
#endif
        break;

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
    auto [encrypted, err] = mxb::load_file<std::vector<uint8_t>>(m_keystore);

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
    else if (!err.empty())
    {
        MXB_ERROR("Failed to load keys: %s", err.c_str());
    }
    else
    {
        // No data and no errors, the file did not exist.
        mxb_assert(access(m_keystore.c_str(), F_OK) != 0 && errno == ENOENT);
        ok = true;
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
        auto err = mxb::save_file(m_keystore, ciphertext.data(), ciphertext.size());

        if (!err.empty())
        {
            ok = false;
            MXB_ERROR("Failed to save keystore: %s", err.c_str());
        }
    }

    return ok;
}
}

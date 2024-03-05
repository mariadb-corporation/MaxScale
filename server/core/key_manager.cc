/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
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

// Always build the file key manager
#include "internal/key_manager_file.hh"

#ifdef BUILD_KMIP_KEY_MANAGER
#include "internal/key_manager_kmip.hh"
#endif

#ifdef BUILD_VAULT_KEY_MANAGER
#include "internal/key_manager_vault.hh"
#endif

namespace
{
struct ThisUnit
{
    std::mutex                       lock;
    std::shared_ptr<mxs::KeyManager> manager;
};

ThisUnit this_unit;
}

namespace maxscale
{

// static
mxs::config::Specification* KeyManager::specification(KeyManager::Type type)
{
    mxs::config::Specification* rval = nullptr;

    switch (type)
    {
    case KeyManager::Type::FILE:
        rval = FileKey::specification();
        break;

    case KeyManager::Type::KMIP:
#ifdef BUILD_KMIP_KEY_MANAGER
        rval = KMIPKey::specification();
#else
        MXB_ERROR("KMIP key manager is not included in this MaxScale installation.");
#endif
        break;

    case KeyManager::Type::VAULT:
#ifdef BUILD_VAULT_KEY_MANAGER
        rval = VaultKey::specification();
#else
        MXB_ERROR("Vault key manager is not included in this MaxScale installation.");
#endif
        break;

    case KeyManager::Type::NONE:
        break;

    default:
        mxb_assert(!true);
        break;
    }

    return rval;
}

// static
bool KeyManager::configure()
{
    std::lock_guard guard(this_unit.lock);
    const auto& cnf = mxs::Config::get();
    Type type = cnf.key_manager;
    const auto& opts = cnf.key_manager_options;

    if (type == Type::NONE)
    {
        return !this_unit.manager;
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

    case Type::VAULT:
#ifdef BUILD_VAULT_KEY_MANAGER
        master_key = VaultKey::create(opts);
#else
        MXB_ERROR("Vault key manager is not included in this MaxScale installation.");
#endif
        break;

    default:
        mxb_assert(!true);
        break;
    }

    bool ok = false;

    if (master_key)
    {
        ok = true;
        this_unit.manager.reset(new KeyManager(type, opts, std::move(master_key)));
    }

    return ok;
}

std::shared_ptr<KeyManager> key_manager()
{
    std::lock_guard guard(this_unit.lock);
    return this_unit.manager;
}

KeyManager::KeyManager(Type type, mxs::ConfigParameters options, std::unique_ptr<MasterKey> master_key)
    : m_master_key(std::move(master_key))
    , m_type(type)
    , m_options(std::move(options))
{
}

std::tuple<bool, uint32_t, std::vector<uint8_t>>
KeyManager::get_key(const std::string& id, uint32_t version) const
{
    return m_master_key->get_key(id, version);
}
}

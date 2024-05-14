/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/secrets.hh>
#include <maxscale/config2.hh>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <tuple>
#include <mutex>

namespace maxscale
{
class KeyManager
{
public:

    enum class Type
    {
        // No key manager
        NONE,

        // File based key manager, stores keys locally on disk. Relatively unsafe, use only if you trust file
        // system security. Read https://mariadb.com/kb/en/file-key-management-encryption-plugin/ for more
        // information.
        FILE,

        // KMIP key manager, reads keys from a remote KMIP server.
        KMIP,

        // HashiCorp Vault key manager, reads keys from a Vault server. Supports versioned master keys.
        VAULT,
    };

    // An abstract class for handling the encryption of the keystore file.
    class MasterKey
    {
    public:
        virtual ~MasterKey() = default;

        static constexpr uint32_t NO_VERSIONING = 0;

        /**
         * Get the Specification for a MasterKey
         *
         * @return The Specification for this MasterKey
         */
        static mxs::config::Specification* specification();

        /**
         * Get the master encryption key
         *
         * @param id      The key ID to get
         * @param version The version of the key to return. If the value is 0, the latest version is returned.
         *                Otherwise the requested version is returned if found.
         *
         * @return Whether the key retrieval was successful, the key version and the encryption key itself. If
         *         the MasterKey implementation does not support key versioning, it must return NO_VERSIONING
         *         as the version. It must also treat any requests for version other than 0 as missing keys,
         *         that is, return false as the first value.
         */
        virtual std::tuple<bool, uint32_t, std::vector<uint8_t>>
        get_key(const std::string& id, uint32_t version) const = 0;
    };

    /**
     * Get the Specification for the given key manager
     *
     * @param type The key manager type
     *
     * @return The Specification for the type, if one is found. If a key manager is not enabled or is not
     *         valid, the function returns null.
     */
    static mxs::config::Specification* specification(Type type);

    /**
     * Configure the key manager
     *
     * @return True if the key manager was successful configured
     */
    static bool configure();

    /**
     * Get master encryption key
     *
     * Get the encryption key used to encrypt the keyring. This is only available for MasterKey
     * implementations that provide access the encryption keys. The ones that do not support it will always
     * return false from this.
     *
     * @param id      The key ID to get
     * @param version The version of the key to use. If the value is 0, the newest key
     *                is returned. If the MasterKey implementation does not support versioning, any requests
     *                for versions other than 0 will fail.
     *
     * @return Whether key retrieval was successful, the key version and the encryption key itself. MasterKey
     *         implementations that do not support versioning return MasterKey::NO_VERSIONING as the version.
     */
    std::tuple<bool, uint32_t, std::vector<uint8_t>> get_key(const std::string& id,
                                                             uint32_t version = 0) const;

private:
    KeyManager(Type type, mxs::ConfigParameters options, std::unique_ptr<MasterKey> master_key);

    std::unique_ptr<MasterKey> m_master_key;    // MasterKey implementation
    Type                       m_type;          // Key manager type
    mxs::ConfigParameters      m_options;       // The key manager options
};

/**
 * Get the global key manager
 *
 * @return The global key manager if one is configured, otherwise an empty shared_ptr.
 */
std::shared_ptr<KeyManager> key_manager();
}

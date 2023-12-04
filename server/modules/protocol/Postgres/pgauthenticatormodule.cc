/*
 * Copyright (c) 2023 MariaDB plc
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

#include "pgauthenticatormodule.hh"
#include <maxscale/protocol/postgresql/scram.hh>
#include <maxscale/utils.hh>
#include <gsasl.h>

std::string PgAuthenticatorModule::supported_protocol() const
{
    return MXB_MODULE_NAME;
}

bool AuthIdEntry::operator==(const AuthIdEntry& rhs) const
{
    return name == rhs.name && password == rhs.password && super == rhs.super && inherit == rhs.inherit
           && can_login == rhs.can_login;
}

namespace postgres
{
ScramSecrets get_scram_secrets(const std::string& pw, const std::array<uint8_t, SCRAM_SALT_SIZE>& salt)
{
    ScramSecrets secrets;

    MXB_AT_DEBUG(int rc = ) gsasl_scram_secrets_from_password(
        GSASL_HASH_SHA256, pw.c_str(), SCRAM_ITER_COUNT, (const char*)salt.data(), salt.size(),
        (char*)secrets.salted_pw.data(), (char*)secrets.client_key.data(),
        (char*)secrets.server_key.data(), (char*)secrets.stored_key.data());
    mxb_assert(rc == GSASL_OK);

    return secrets;
}

std::string salt_password(const std::string& pw)
{
    // Create a new random salt
    ScramSalt salt{};
    gsasl_random((char*)salt.data(), salt.size());

    ScramSecrets secrets = get_scram_secrets(pw, salt);

    // The password is stored as: SCRAM-SHA-256$<iteration count>:<salt>$<StoredKey>:<ServerKey>
    return mxb::cat("SCRAM-SHA-256$",
                    std::to_string(SCRAM_ITER_COUNT), ":", mxs::to_base64(salt), "$",
                    mxs::to_base64(secrets.stored_key), ":", mxs::to_base64(secrets.server_key));
}
}

/*
 * Copyright (c) 2023 MariaDB plc
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

#include "common.hh"
#include <maxscale/utils.hh>

std::optional<ScramUser> parse_scram_password(std::string_view pw)
{
    /**
     * The passwords are of the following form:
     *
     *   SCRAM-SHA-256$<iteration count>:<salt>$<StoredKey>:<ServerKey>
     *
     * Here's an example hash for the user "maxuser" with the password "maxpwd":
     *
     * SCRAM-SHA-256$4096:fcyQNek/oqCBB5+HBZYCBw==$IyjIV2enCngF0p4pOouPlvKyISzmHFdoXeM0V/+nUr4=:+vF1tu+XCwHxdmfo1X3zpgvDXpCx06LJjJ2emDgXCs0=
     */

    std::string_view prefix = "SCRAM-SHA-256$";

    if (pw.substr(0, prefix.size()) == prefix)
    {
        pw.remove_prefix(prefix.size());

        auto [iter_and_salt, stored_and_server] = mxb::split(pw, "$");
        const auto [iter, salt] = mxb::split(iter_and_salt, ":");
        const auto [stored, server] = mxb::split(stored_and_server, ":");

        if (!iter.empty() && !salt.empty() && !stored.empty() && !server.empty())
        {
            ScramUser user;

            user.iter = iter;
            user.salt = salt;
            auto stored_bin = mxs::from_base64(stored);
            auto server_bin = mxs::from_base64(server);

            if (stored_bin.size() == SHA256_DIGEST_LENGTH
                && server_bin.size() == SHA256_DIGEST_LENGTH)
            {
                memcpy(user.stored_key.data(), stored_bin.data(), stored_bin.size());
                memcpy(user.server_key.data(), server_bin.data(), server_bin.size());
                return user;
            }
        }
    }

    return {};
}

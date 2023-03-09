/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "postgresprotocol.hh"
#include <maxscale/authenticator.hh>

using Digest = std::array<uint8_t, SHA256_DIGEST_LENGTH>;

struct ScramUser
{
    std::string iter;
    std::string salt;
    Digest      stored_key{};
    Digest      server_key{};
};

class PgAuthenticatorModule : public mxs::AuthenticatorModule
{
public:
    ~PgAuthenticatorModule();

    std::string supported_protocol() const override;

    std::string name() const override;
};

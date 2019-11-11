/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once
#include "pam_auth.hh"

#include <cstdint>
#include <string>
#include <vector>
#include "pam_backend_session.hh"
#include "pam_auth_common.hh"
#include <maxscale/protocol/mariadb/protocol_classes.hh>

class PamAuthenticatorModule;

/** Client authenticator PAM-specific session data */
class PamClientAuthenticator : public mariadb::ClientAuthenticatorT<PamAuthenticatorModule>
{
public:
    using StringVector = std::vector<std::string>;
    static mariadb::SClientAuth create(PamAuthenticatorModule* instance);

    AuthRes authenticate(DCB* client, const mariadb::UserEntry* entry) override;
    bool    extract(GWBUF* read_buffer, MYSQL_session* session) override;

private:
    PamClientAuthenticator(PamAuthenticatorModule* instance);

    maxscale::Buffer create_auth_change_packet() const;

    enum class State
    {
        INIT,
        ASKED_FOR_PW,
        PW_RECEIVED,
        DONE
    };

    State    m_state {State::INIT};   /**< Authentication state */
    uint8_t  m_sequence {0};          /**< The next packet seqence number */
};

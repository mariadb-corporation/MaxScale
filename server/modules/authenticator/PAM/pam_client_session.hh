/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once
#include "pam_auth.hh"

#include <stdint.h>
#include <string>
#include <vector>
#include <maxsql/sqlite.hh>
#include "pam_backend_session.hh"
#include "pam_auth_common.hh"
#include <maxscale/protocol/mariadb/protocol_classes.hh>

class PamAuthenticatorModule;

/** Client authenticator PAM-specific session data */
class PamClientAuthenticator : public mxs::ClientAuthenticatorT<PamAuthenticatorModule>
{
public:
    using StringVector = std::vector<std::string>;
    static std::unique_ptr<mxs::ClientAuthenticator> create(PamAuthenticatorModule* instance);

    int  authenticate(DCB* client) override;
    bool extract(DCB* dcb, GWBUF* read_buffer) override;

    bool ssl_capable(DCB* client) override;

private:
    PamClientAuthenticator(PamAuthenticatorModule* instance);
    void get_pam_user_services(const DCB* dcb,
                               const MYSQL_session* session,
                               StringVector* services_out);
    bool user_can_access_db(const std::string& user, const std::string& host, const std::string& target_db);
    bool role_can_access_db(const std::string& role, const std::string& target_db);

    maxscale::Buffer create_auth_change_packet() const;

    enum class State
    {
        INIT,
        ASKED_FOR_PW,
        PW_RECEIVED,
        DONE
    };

    SQLite m_sqlite;   /**< SQLite3 database handle */

    State    m_state {State::INIT};   /**< Authentication state */
    uint8_t  m_sequence {0};          /**< The next packet seqence number */
};

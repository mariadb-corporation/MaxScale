/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
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
#include <maxscale/sqlite3.h>
#include "pam_instance.hh"
#include "../pam_auth_common.hh"

/** Client authenticator PAM-specific session data */
class PamClientSession
{
public:
    PamClientSession(const PamClientSession& orig) = delete;
    PamClientSession& operator=(const PamClientSession&) = delete;

    using StringVector = std::vector<std::string>;
    static PamClientSession* create(const PamInstance& inst);

    int  authenticate(DCB* client);
    bool extract(DCB* dcb, GWBUF* read_buffer);

private:
    PamClientSession(const PamInstance& instance, SQLite::SSQLite sqlite);
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

    const PamInstance&    m_instance;              /**< Authenticator instance */
    SQLite::SSQLite const m_sqlite;                /**< SQLite3 database handle */

    State    m_state {State::INIT};   /**< Authentication state */
    uint8_t  m_sequence {0};          /**< The next packet seqence number */
};

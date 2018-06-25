#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
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
    PamClientSession(const PamClientSession& orig);
    PamClientSession& operator=(const PamClientSession&);
public:
    typedef std::vector<std::string> StringVector;
    static PamClientSession* create(const PamInstance& inst);
    ~PamClientSession();
    int authenticate(DCB* client);
    bool extract(DCB *dcb, GWBUF *read_buffer);
private:
    PamClientSession(sqlite3* dbhandle, const PamInstance& instance);
    void get_pam_user_services(const DCB* dcb, const MYSQL_session* session,
                               StringVector* services_out);
    maxscale::Buffer create_auth_change_packet() const;

    pam_auth_state m_state; /**< Authentication state*/
    uint8_t m_sequence; /**< The next packet seqence number */
    sqlite3* const m_dbhandle; /**< SQLite3 database handle */
    const PamInstance& m_instance; /**< Authenticator instance */
};



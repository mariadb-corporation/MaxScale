/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once
#include "pam_backend_auth.hh"
#include "../pam_auth_common.hh"

class PamBackendSession
{
public:
    PamBackendSession(const PamBackendSession& orig) = delete;
    PamBackendSession& operator=(const PamBackendSession&) = delete;

    PamBackendSession();
    bool extract(DCB* dcb, GWBUF* buffer);
    int  authenticate(DCB* dcb);

private:
    bool send_client_password(DCB* dcb);
    bool parse_authswitchreq(const uint8_t** data, const uint8_t* end);
    bool parse_password_prompt(const uint8_t** data, const uint8_t* end);

    enum class State
    {
        INIT,
        RECEIVED_PROMPT,
        PW_SENT,
        DONE
    };

    State         m_state {State::INIT}; /**< Authentication state */
    uint8_t       m_sequence {0};        /**< The next packet sequence number */
    std::string   m_servername;          /**< Backend name, for logging */
    std::string   m_clienthost;          /**< Client name & host, for logging */
};

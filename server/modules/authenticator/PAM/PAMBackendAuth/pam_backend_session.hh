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
#include "pam_backend_auth.hh"

#include <stdint.h>
#include "../pam_auth_common.hh"

class PamBackendSession
{
    PamBackendSession(const PamBackendSession& orig);
    PamBackendSession& operator=(const PamBackendSession&);
public:
    PamBackendSession();
    bool extract(DCB *dcb, GWBUF *buffer);
    int authenticate(DCB *dcb);

private:
    bool send_client_password(DCB *dcb);

    pam_auth_state m_state; /**< Authentication state*/
    uint8_t m_sequence; /**< The next packet seqence number */
};

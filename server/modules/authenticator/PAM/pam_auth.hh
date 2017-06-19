#pragma once
#ifndef _PAM_AUTH_H
#define _PAM_AUTH_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>

#include <string>
#include <vector>
#include <stdint.h>
#include <stddef.h>
#include <security/_pam_types.h>
#include <maxscale/sqlite3.h>
#include <maxscale/thread.h>

typedef std::vector<std::string> StringArray;

/** Name if the PAM client helper plugin */
const char DIALOG[] = "dialog"; // or "mysql_clear_password"
const char PASSWORD[] = "Password: ";

/** PAM authentication states */
enum pam_auth_state
{
    PAM_AUTH_INIT = 0,
    PAM_AUTH_DATA_SENT,
    PAM_AUTH_OK,
    PAM_AUTH_FAILED
};

// Magic numbers from server source https://github.com/MariaDB/server/blob/10.2/plugin/auth_pam/auth_pam.c
enum dialog_plugin_msg_types
{
    DIALOG_ECHO_ENABLED = 2,
    DIALOG_ECHO_DISABLED = 4
};

/** Common structure for both backend and client authenticators */
struct PamSession
{
    PamSession()
    {
        m_state = PAM_AUTH_INIT;
        m_sequence = 0;
        m_dbhandle = NULL;
    }
    ~PamSession()
    {
        sqlite3_close_v2(m_dbhandle);
    }

    pam_auth_state m_state; /**< Authentication state*/
    uint8_t m_sequence; /**< The next packet seqence number */
    sqlite3* m_dbhandle; /**< SQLite3 database handle */
};

#endif

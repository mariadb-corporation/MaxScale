/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/*
 * Common definitions and includes for PAM authenticator.
 */
#define MXS_MODULE_NAME "PAMAuth"

#include <maxscale/ccdefs.hh>
#include <string>

extern const std::string DIALOG;    /* PAM client helper plugin name */
extern const int DIALOG_SIZE;       /* The total storage required */
extern const std::string CLEAR_PW;  /* Alternative plugin */
extern const int CLEAR_PW_SIZE;
extern const std::string EXP_PW_QUERY;      /* Expected normal password query */
extern const std::string PASSWORD_QUERY;    /* Standard password query sent to client */
extern const std::string TWO_FA_QUERY;      /* 2FA query sent to client */

/* Magic numbers from server source
 * https://github.com/MariaDB/server/blob/10.2/plugin/auth_pam/auth_pam.c */
enum dialog_plugin_msg_types
{
    DIALOG_ECHO_ENABLED  = 2,
    DIALOG_ECHO_DISABLED = 4
};

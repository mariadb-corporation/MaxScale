/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
 #pragma once

/**
 * Common declarations for both PAMAuth and PAMBackendAuth
 */
#include <maxscale/ccdefs.hh>
#include <string>

extern const std::string DIALOG;
extern const std::string PASSWORD;
extern const int DIALOG_SIZE;
extern const char GENERAL_ERRMSG[];

/** PAM authentication states */
enum pam_auth_state
{
    PAM_AUTH_INIT = 0,
    PAM_AUTH_DATA_SENT,
    PAM_AUTH_OK,
    PAM_AUTH_FAILED
};

/* Magic numbers from server source
   https://github.com/MariaDB/server/blob/10.2/plugin/auth_pam/auth_pam.c */
enum dialog_plugin_msg_types
{
    DIALOG_ECHO_ENABLED = 2,
    DIALOG_ECHO_DISABLED = 4
};


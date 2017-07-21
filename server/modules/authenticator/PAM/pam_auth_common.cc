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

/**
 * Set values for constants shared between both PAMAuth and PAMBackendAuth
 */
#include "pam_auth_common.hh"

/* PAM client helper plugin name, TODO: add support for "mysql_clear_password" */
const std::string DIALOG = "dialog";
/* The total storage required */
const int DIALOG_SIZE = DIALOG.length() + 1;
/* First query from server */
const std::string PASSWORD = "Password: ";
const char GENERAL_ERRMSG[] = "Only simple password-based PAM authentication with one call "
                              "to the conversation function is supported.";

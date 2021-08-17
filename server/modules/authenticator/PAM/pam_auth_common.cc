/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Set values for constants shared between both PAMAuth and PAMBackendAuth
 */
#include "pam_auth_common.hh"

const std::string DIALOG = "dialog";
const int DIALOG_SIZE = DIALOG.length() + 1;
const std::string CLEAR_PW = "mysql_clear_password";
const int CLEAR_PW_SIZE = CLEAR_PW.length() + 1;
const std::string EXP_PW_QUERY = "Password";
const std::string PASSWORD_QUERY = "Password: ";
const std::string TWO_FA_QUERY = "Verification code: ";

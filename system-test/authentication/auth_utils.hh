/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxtest/testconnections.hh>
#include <string>

namespace auth_utils
{
enum class Ssl {ON, OFF};
void try_conn(TestConnections& test, int port, Ssl ssl, const std::string& user, const std::string& pass,
              bool expect_success);
void copy_basic_pam_cfg(mxt::MariaDBServer* server);
void remove_basic_pam_cfg(mxt::MariaDBServer* server);
void create_basic_pam_user(mxt::MariaDBServer* server, const std::string& user, const std::string& pw);
void delete_basic_pam_user(mxt::MariaDBServer* server, const std::string& user);
}

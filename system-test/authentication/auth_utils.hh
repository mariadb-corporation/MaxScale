/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
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
void copy_basic_pam_cfg(mxt::Node& node);
void remove_basic_pam_cfg(mxt::Node& node);
void create_basic_pam_user(mxt::MariaDBServer* server, const std::string& user);
void delete_basic_pam_user(mxt::MariaDBServer* server, const std::string& user);
void prepare_basic_pam_user(const std::string& user, const std::string& pw, mxt::MaxScale* mxs,
                            mxt::MariaDBServer* master, const std::vector<mxt::MariaDBServer*>& slaves);
void prepare_pam_user(const std::string& user, const std::string& pw, const std::string& service,
                      mxt::MaxScale* mxs, mxt::MariaDBServer* master,
                      const std::vector<mxt::MariaDBServer*>& slaves);
void remove_pam_user(const std::string& user, mxt::MaxScale* mxs, mxt::MariaDBServer* master,
                     const std::vector<mxt::MariaDBServer*>& slaves);
void install_pam_plugin(mxt::MariaDBServer* server);
void uninstall_pam_plugin(mxt::MariaDBServer* server);
}

/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxtest/ccdefs.hh>

/**
 * @brief execute_cmd Execute shell command
 * @param cmd Command line
 * @param res Pointer to variable that will contain command console output (stdout)
 * @return Process exit code
 */
int execute_cmd(char * cmd, char ** res);
namespace maxtest
{
class Node;
}

namespace jdbc
{
enum class ConnectorVersion
{
    MARIADB_250, MARIADB_270, MYSQL606
};

std::string to_string(ConnectorVersion vrs);

struct Result
{
    bool success {false};
    std::string output;
};
Result test_connection(ConnectorVersion vrs, const std::string& host, int port,
                       const std::string& user, const std::string& pass1, const std::string& pass2,
                       const std::string& query);

Result test_connection(ConnectorVersion vrs, const std::string& host, int port,
                       const std::string& user, const std::string& passwd,
                       const std::string& query);
}

namespace pam
{
void copy_user_map_lib(mxt::Node& source, mxt::Node& dst);
void delete_user_map_lib(mxt::Node& dst);
void copy_map_config(mxt::Node& vm);
void delete_map_config(mxt::Node& vm);
}

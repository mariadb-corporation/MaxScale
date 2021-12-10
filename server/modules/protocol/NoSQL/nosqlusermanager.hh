/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include "nosqlbase.hh"
#include <memory>
#include <maxscale/sqlite3.h>

namespace nosql
{

class UserManager
{
public:
    ~UserManager();

    static std::unique_ptr<UserManager> create(const std::string& name);

    const std::string& path() const
    {
        return m_path;
    }

    bool add_user(const std::string& user,
                  const string_view& pwd,
                  const bsoncxx::array::view& roles);

    bool remove_user(const std::string& user);

    bool get_pwd(const std::string& user, std::string* pPwd) const;

    bool user_exists(const std::string& user) const
    {
        return get_pwd(user, nullptr);
    }

private:
    UserManager(std::string path, sqlite3* pDb);

    std::string m_path;
    sqlite3&    m_db;
};

}

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

#include "nosqlusermanager.hh"
#include <maxscale/paths.hh>
#include <maxscale/utils.hh>

using namespace std;

namespace
{

const int SCHEMA_VERSION = 1;

//
// User Database
//
static const char SQL_CREATE[] =
    "CREATE TABLE IF NOT EXISTS accounts (user TEXT UNIQUE, pwd TEXT, salt_b64 TEXT)";

static const char SQL_INSERT_HEAD[] =
    "INSERT INTO accounts (user, pwd, salt_b64) VALUES ";

static const char SQL_DELETE_HEAD[] =
    "DELETE FROM accounts WHERE user = ";

static const char SQL_SELECT_ONE_HEAD[] =
    "SELECT user, pwd, salt_b64 FROM accounts WHERE user = ";

int select_one_cb(void* pData, int nColumns, char** ppColumn, char** ppNames)
{
    mxb_assert(nColumns == 3);

    auto* pUsers = static_cast<vector<nosql::UserManager::User>*>(pData);

    nosql::UserManager::User user;
    user.user = ppColumn[0];
    user.pwd = ppColumn[1];
    user.salt_b64 = ppColumn[2];
    user.salt = mxs::from_base64(user.salt_b64);

    pUsers->push_back(user);

    return 0;
}

bool create_schema(sqlite3* pDb)
{
    char* pError = nullptr;
    int rv = sqlite3_exec(pDb, SQL_CREATE, nullptr, nullptr, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not initialize sqlite3 database: %s", pError ? pError : "Unknown error");
    }

    return rv == SQLITE_OK;
}

sqlite3* open_or_create_db(const std::string& path)
{
    sqlite3* pDb = nullptr;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_CREATE;
    int rv = sqlite3_open_v2(path.c_str(), &pDb, flags, nullptr);

    if (rv == SQLITE_OK)
    {
        if (create_schema(pDb))
        {
            MXS_NOTICE("sqlite3 database %s open/created and initialized.", path.c_str());
        }
        else
        {
            MXS_ERROR("Could not create schema in sqlite3 database %s.", path.c_str());

            if (unlink(path.c_str()) != 0)
            {
                MXS_ERROR("Failed to delete database %s that could not be properly "
                          "initialized. It should be deleted manually.", path.c_str());
                sqlite3_close_v2(pDb);
                pDb = nullptr;
            }
        }
    }
    else
    {
        if (pDb)
        {
            // Memory allocation failure is explained by the caller. Don't close the handle, as the
            // caller will still use it even if open failed!!
            MXS_ERROR("Opening/creating the sqlite3 database %s failed: %s",
                      path.c_str(), sqlite3_errmsg(pDb));
        }
        MXS_ERROR("Could not open sqlite3 database for storing user information.");
    }

    return pDb;
}


}


namespace nosql
{

UserManager::UserManager(string path, sqlite3* pDb)
    : m_path(std::move(path))
    , m_db(*pDb)
{
}

UserManager::~UserManager()
{
    sqlite3_close_v2(&m_db);
}

//static
unique_ptr<UserManager> UserManager::create(const string& name)
{
    nosql::UserManager* pThis = nullptr;

    string path = mxs::datadir();

    path += "/nosqlprotocol/";
    path += name;

    if (mxs_mkdir_all(path.c_str(), 0744))
    {
        path += "/users-v";
        path += std::to_string(SCHEMA_VERSION);
        path += ".db";

        sqlite3* pDb = open_or_create_db(path);

        if (pDb)
        {
            pThis = new UserManager(std::move(path), pDb);
        }
        else
        {
            // The handle will be null, *only* if the opening fails due to a memory
            // allocation error.
            MXS_ALERT("sqlite3 memory allocation failed, nosqlprotocol cannot continue.");
        }
    }
    else
    {
        MXS_ERROR("Could not create the directory %s, needed by the listener %s "
                  "for storing nosqlprotocol user information.",
                  path.c_str(), name.c_str());
    }

    return unique_ptr<UserManager>(pThis);
}

bool UserManager::add_user(const string& user,
                           const string_view& pwd,
                           const string& salt_b64,
                           const bsoncxx::array::view& roles)
{
    ostringstream ss;
    ss << SQL_INSERT_HEAD << "(\"" << user << "\", \"" << pwd << "\", \"" << salt_b64 << "\")";

    string sql = ss.str();

    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), nullptr, nullptr, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not add user '%s' to local database: %s",
                  user.c_str(),
                  pError ? pError : "Unknown error");
    }

    return rv == SQLITE_OK;
}

bool UserManager::remove_user(const string& user)
{
    ostringstream ss;
    ss << SQL_DELETE_HEAD << "\"" << user << "\"";

    string sql = ss.str();

    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), nullptr, nullptr, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not remove user '%s' from local database: %s",
                  user.c_str(),
                  pError ? pError : "Unknown error");
    }

    return rv == SQLITE_OK;
}

bool UserManager::get_user(const std::string& user, User* pUser) const
{
    ostringstream ss;
    ss << SQL_SELECT_ONE_HEAD << "\"" << user << "\"";

    string sql = ss.str();

    vector<User> users;
    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), select_one_cb, &users, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not get data for user '%s' from local database: %s",
                  user.c_str(),
                  pError ? pError : "Unknown error");
    }

    if (!users.empty() && pUser)
    {
        mxb_assert(users.size() == 1);
        *pUser = users.front();
    }

    return !users.empty();
}

bool UserManager::get_pwd(const std::string& user, std::string* pPwd) const
{
    User data;
    bool rv = get_user(user, &data);

    if (rv)
    {
        *pPwd = data.pwd;
    }

    return rv;
}

bool UserManager::get_salt_b64(const std::string& user, std::string* pSalt_b64) const
{
    User data;
    bool rv = get_user(user, &data);

    if (rv)
    {
        *pSalt_b64 = data.salt_b64;
    }

    return rv;
}

}

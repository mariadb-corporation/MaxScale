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
#include <map>
#include <maxscale/paths.hh>
#include <maxscale/utils.hh>

using namespace std;

namespace
{

const int SCHEMA_VERSION = 1;

bool get_string_role_id(const string& key, const string& role_name, nosql::role::Id* pId)
{
    bool rv = nosql::role::from_string(role_name, pId);

    if (!rv)
    {
        MXS_ERROR("Role '%s' of '%s' is unknown.", role_name.c_str(), key.c_str());
    }

    return rv;
}

bool get_object_role(const string& key, const mxb::Json& json, nosql::role::Role* pRole)
{
    bool rv = false;

    string db;
    if (json.try_get_string("db", &db))
    {
        string role_name;
        if (json.try_get_string("role", &role_name))
        {
            nosql::role::Id id;
            rv = get_string_role_id(key, role_name, &id);

            if (rv)
            {
                pRole->db = std::move(db);
                pRole->id = id;
            }
        }
        else
        {
            MXS_ERROR("An object role of '%s' does not have the 'role' field, or "
                      "the value is not a string.", key.c_str());
        }
    }
    else
    {
        MXS_ERROR("An object role of '%s' does not have the 'db' field.", key.c_str());
    }

    return rv;
}

bool get_roles(const string& key,
               const string& db,
               const char* zJson,
               vector<nosql::role::Role>* pRoles)
{
    bool rv = false;

    mxb::Json json;

    if (json.load_string(zJson))
    {
        if (json.type() == mxb::Json::Type::ARRAY)
        {
            vector<mxb::Json> elements = json.get_array_elems();
            vector<nosql::role::Role> roles;

            auto it = elements.begin();
            for (; it != elements.end(); ++it)
            {
                auto element = *it;
                auto type = element.type();

                nosql::role::Role role;

                if (type == mxb::Json::Type::STRING)
                {
                    role.db = db;
                    if (!get_string_role_id(key, element.get_string(), &role.id))
                    {
                        break;
                    }
                }
                else if (type == mxb::Json::Type::OBJECT)
                {
                    if (!get_object_role(key, element, &role))
                    {
                        break;
                    }
                }
                else
                {
                    break;
                }

                roles.push_back(role);
            }

            if (it == elements.end())
            {
                pRoles->swap(roles);
                rv = true;
            }
            else
            {
                MXS_ERROR("Roles '%s' of '%s' is a JSON array, but all elements are not strings.",
                          zJson, key.c_str());
            }
        }
        else
        {
            MXS_ERROR("Roles '%s' of '%s' is JSON, but not an array.", zJson, key.c_str());
        }
    }
    else
    {
        MXS_ERROR("Roles '%s' of '%s' is not valid JSON: %s", zJson, key.c_str(), json.error_msg().c_str());
    }

    return rv;
}

//
// User Database
//
static const char SQL_CREATE[] =
    "CREATE TABLE IF NOT EXISTS accounts "
    "(scoped_user TEXT UNIQUE, scope TEXT, user TEXT, pwd TEXT, salt_b64 TEXT, roles TEXT)";

static const char SQL_INSERT_HEAD[] =
    "INSERT INTO accounts (scoped_user, scope, user, pwd, salt_b64, roles) VALUES ";

static const char SQL_DELETE_HEAD[] =
    "DELETE FROM accounts WHERE scoped_user = ";

static const char SQL_SELECT_ONE_HEAD[] =
    "SELECT scoped_user, scope, user, pwd, salt_b64, roles FROM accounts WHERE scoped_user = ";

static const char SQL_SELECT_ALL_USERS[] =
    "SELECT scoped_user, scope, user, pwd, salt_b64, roles FROM accounts";

static const char SQL_SELECT_ALL_SCOPE_USERS_HEAD[] =
    "SELECT scoped_user, scope, user, pwd, salt_b64, roles FROM accounts WHERE scope = ";

static const char SQL_SELECT_SOME_SCOPE_USERS_HEAD[] =
    "SELECT scoped_user, scope, user, pwd, salt_b64, roles FROM accounts WHERE ";

int select_cb(void* pData, int nColumns, char** ppColumn, char** ppNames)
{
    mxb_assert(nColumns == 6);

    auto* pInfos = static_cast<vector<nosql::UserManager::UserInfo>*>(pData);

    nosql::UserManager::UserInfo info;
    info.scoped_user = ppColumn[0];
    info.scope = ppColumn[1];
    info.user = ppColumn[2];
    info.pwd = ppColumn[3];
    info.salt_b64 = ppColumn[4];
    info.salt = mxs::from_base64(info.salt_b64);

    vector<nosql::role::Role> roles;

    if (get_roles(info.scoped_user, info.scope, ppColumn[5], &roles))
    {
        info.roles = std::move(roles);

        pInfos->push_back(info);
    }
    else
    {
        MXS_WARNING("Ignoring user '%s'.", info.user.c_str());
    }

    return 0;
}

bool create_schema(sqlite3* pDb)
{
    char* pError = nullptr;
    int rv = sqlite3_exec(pDb, SQL_CREATE, nullptr, nullptr, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not initialize sqlite3 database: %s", pError ? pError : "Unknown error");
        sqlite3_free(pError);
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

namespace role
{

namespace
{

const map<string, Id> roles =
{
    { "dbAdmin",   Id::DB_ADMIN },
    { "read",      Id::READ },
    { "readWrite", Id::READ_WRITE }
};

}

}

string role::to_string(role::Id id)
{
    for (const auto& kv : roles)
    {
        if (id == kv.second)
        {
            return kv.first;
        }
    }

    mxb_assert(!true);

    return "unknown";
}

bool role::from_string(const string& key, role::Id* pValue)
{
    auto it = roles.find(key);

    bool found = (it != roles.end());

    if (found)
    {
        *pValue = it->second;
    }

    return found;
}

string role::to_json(const std::vector<role::Role>& roles)
{
    ostringstream ss;

    ss << "[";

    auto it = roles.begin();
    for (; it != roles.end(); ++it)
    {
        const auto& role = *it;

        if (it != roles.begin())
        {
            ss << ", ";
        }

        ss << "{"
           << "\"db\": \"" << role.db << "\", "
           << "\"role\": \"" << to_string(role.id) << "\""
           << "}";
    }

    ss << "]";

    return ss.str();
}

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

bool UserManager::add_user(const string& scope,
                           const string_view& user,
                           const string_view& pwd,
                           const string& salt_b64,
                           const vector<role::Role>& roles)
{
    string scoped_user = scope + "." + string(user.data(), user.length());

    ostringstream ss;
    ss << SQL_INSERT_HEAD << "('"
       << scoped_user << "', '"
       << scope << "', '"
       << user << "', '"
       << pwd << "', '"
       << salt_b64 << "', '"
       << role::to_json(roles)
       << "')";

    string sql = ss.str();

    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), nullptr, nullptr, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not add user '%s' to local database: %s",
                  scoped_user.c_str(),
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return rv == SQLITE_OK;
}

bool UserManager::remove_user(const string& scope, const string& user)
{
    string scoped_user = scope + "." + user;

    ostringstream ss;
    ss << SQL_DELETE_HEAD << "\"" << scoped_user << "\"";

    string sql = ss.str();

    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), nullptr, nullptr, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not remove user '%s' from local database: %s",
                  user.c_str(),
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return rv == SQLITE_OK;
}

bool UserManager::get_info(const string& scope, const string& user, UserInfo* pInfo) const
{
    string scoped_user = scope + "." + user;

    return get_scoped_info(scoped_user, pInfo);
}

bool UserManager::get_scoped_info(const string& scoped_user, UserInfo* pInfo) const
{
    ostringstream ss;
    ss << SQL_SELECT_ONE_HEAD << "\"" << scoped_user << "\"";

    string sql = ss.str();

    vector<UserInfo> infos;
    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), select_cb, &infos, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not get data for user '%s' from local database: %s",
                  scoped_user.c_str(),
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    if (!infos.empty() && pInfo)
    {
        mxb_assert(infos.size() == 1);
        *pInfo = infos.front();
    }

    return !infos.empty();
}

bool UserManager::get_pwd(const string& scope, const string& user, std::string* pPwd) const
{
    UserInfo info;
    bool rv = get_info(scope, user, &info);

    if (rv)
    {
        *pPwd = info.pwd;
    }

    return rv;
}

bool UserManager::get_salt_b64(const string& scope, const string& user, std::string* pSalt_b64) const
{
    UserInfo info;
    bool rv = get_info(scope, user, &info);

    if (rv)
    {
        *pSalt_b64 = info.salt_b64;
    }

    return rv;
}

vector<UserManager::UserInfo> UserManager::get_infos() const
{
    vector<UserInfo> infos;
    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, SQL_SELECT_ALL_USERS, select_cb, &infos, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not get user data from local database: %s",
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return infos;
}

vector<UserManager::UserInfo> UserManager::get_infos(const std::string& scope) const
{
    ostringstream ss;
    ss << SQL_SELECT_ALL_SCOPE_USERS_HEAD << "\"" << scope << "\"";

    string sql = ss.str();

    vector<UserInfo> infos;
    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), select_cb, &infos, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not get user data from local database: %s",
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return infos;
}

vector<UserManager::UserInfo> UserManager::get_infos(const vector<string>& scoped_users) const
{
    vector<UserInfo> infos;

    if (!scoped_users.empty())
    {
        ostringstream ss;
        ss << SQL_SELECT_SOME_SCOPE_USERS_HEAD;

        auto it = scoped_users.begin();
        for (; it != scoped_users.end(); ++it)
        {
            if (it != scoped_users.begin())
            {
                ss << " OR ";
            }

            ss << "scoped_user = '" << *it << "'";
        }

        string sql = ss.str();

        char* pError = nullptr;
        int rv = sqlite3_exec(&m_db, sql.c_str(), select_cb, &infos, &pError);

        if (rv != SQLITE_OK)
        {
            MXS_ERROR("Could not get user data from local database: %s",
                      pError ? pError : "Unknown error");
            sqlite3_free(pError);
        }
    }

    return infos;
}

}

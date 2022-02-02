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
#include <sys/stat.h>
#include <uuid/uuid.h>
#include <map>
#include <maxscale/paths.hh>
#include <maxscale/utils.hh>
#include "nosqlkeys.hh"

using namespace std;

namespace
{

const int SCHEMA_VERSION = 1;

// Not all uuid/uuid.h appears to have UUID_STR_LEN defined.
const int NOSQL_UUID_STR_LEN = 37;

//
// User Database
//
static const char SQL_CREATE[] =
    "CREATE TABLE IF NOT EXISTS accounts "
    "(mariadb_user TEXT UNIQUE, db TEXT, user TEXT, pwd_sha1_b64 TEXT, host TEXT, "
    "custom_data TEXT, uuid TEXT, "
    "salt_sha1_b64 TEXT, salt_sha256_b64 TEXT, salted_pwd_sha1_b64 TEXT, salted_pwd_sha256_b64 TEXT, "
    "roles TEXT)";

static const char SQL_INSERT_HEAD[] =
    "INSERT INTO accounts "
    "(mariadb_user, db, user, pwd_sha1_b64, host, custom_data, uuid, salt_sha1_b64, salt_sha256_b64, "
    "salted_pwd_sha1_b64, salted_pwd_sha256_b64, roles) "
    "VALUES ";

static const char SQL_DELETE_WHERE_MARIADB_USER_HEAD[] =
    "DELETE FROM accounts WHERE mariadb_user = ";

static const char SQL_DELETE_WHERE_HEAD[] =
    "DELETE FROM accounts WHERE ";

static const char SQL_SELECT_WHERE_MARIADB_USER_HEAD[] =
    "SELECT * FROM accounts WHERE mariadb_user = ";

static const char SQL_SELECT_ALL[] =
    "SELECT * FROM accounts";

static const char SQL_SELECT_WHERE_DB_HEAD[] =
    "SELECT * FROM accounts WHERE db = ";

static const char SQL_SELECT_WHERE_HEAD[] =
    "SELECT * FROM accounts WHERE ";

static const char SQL_SELECT_MARIADB_USER_HOST_WHERE_DB_HEAD[] =
    "SELECT mariadb_user, host FROM accounts WHERE db = ";

static const char SQL_UPDATE_HEAD[] =
    "UPDATE accounts SET ";

static const char SQL_UPDATE_TAIL[] =
    " WHERE mariadb_user = ";


int select_info_cb(void* pData, int nColumns, char** pzColumn, char** pzNames)
{
    mxb_assert(nColumns == 12);

    auto* pInfos = static_cast<vector<nosql::UserManager::UserInfo>*>(pData);

    nosql::UserManager::UserInfo info;
    info.mariadb_user = pzColumn[0];
    info.db = pzColumn[1];
    info.user = pzColumn[2];
    info.pwd_sha1_b64 = pzColumn[3];
    info.host = pzColumn[4];
    info.custom_data = pzColumn[5];
    info.uuid = pzColumn[6];
    info.salt_sha1_b64 = pzColumn[7];
    info.salt_sha256_b64 = pzColumn[8];
    info.salted_pwd_sha1_b64 = pzColumn[9];
    info.salted_pwd_sha256_b64 = pzColumn[10];

    if (!info.salt_sha1_b64.empty())
    {
        info.mechanisms.push_back(nosql::scram::Mechanism::SHA_1);
    }

    if (!info.salt_sha256_b64.empty())
    {
        info.mechanisms.push_back(nosql::scram::Mechanism::SHA_256);
    }

    bool ok = true;

    if (!info.custom_data.empty())
    {
        mxb::Json json;

        if (!json.load_string(info.custom_data) || json.type() != mxb::Json::Type::OBJECT)
        {
            MXB_ERROR("The 'custom_data' field of '%s' is not a JSON object.", info.mariadb_user.c_str());
            ok = false;
        }
    }

    vector<nosql::role::Role> roles;
    if (nosql::role::from_json(pzColumn[11], &roles))
    {
        info.roles = std::move(roles);

        pInfos->push_back(info);
    }
    else
    {
        MXS_ERROR("The 'roles' value of '%s' is not valid.", info.mariadb_user.c_str());
        ok = false;
    }

    if (!ok)
    {
        MXS_WARNING("Ignoring user '%s'.", info.mariadb_user.c_str());
    }

    return 0;
}

int select_mariadb_accounts_cb(void* pData, int nColumns, char** pzColumn, char** pzNames)
{
    mxb_assert(nColumns == 2);

    auto* pMariaDb_accounts = static_cast<vector<nosql::UserManager::MariaDBAccount>*>(pData);

    pMariaDb_accounts->emplace_back(nosql::UserManager::MariaDBAccount { pzColumn[0], pzColumn[1] });

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

const map<string, Id> roles_by_name =
{
#define NOSQL_ROLE(id, value, name) { name, Id:: id },
#include "nosqlrole.hh"
#undef NOSQL_ROLE
};

const map<Id, string> roles_by_id =
{
#define NOSQL_ROLE(id, value, name) { Id:: id, name },
#include "nosqlrole.hh"
#undef NOSQL_ROLE
};

}

}

unordered_map<string, uint32_t> role::to_bitmasks(const vector<role::Role>& roles)
{
    unordered_map<string, uint32_t> bitmasks;

    for (const auto& role : roles)
    {
        bitmasks[role.db] |= role.id;
    }

    return bitmasks;
}

string role::to_string(Id id)
{
    auto it = roles_by_id.find(id);
    mxb_assert(it != roles_by_id.end());

    return it->second;
}

bool role::from_string(const string& key, Id* pValue)
{
    auto it = roles_by_name.find(key);

    bool found = (it != roles_by_name.end());

    if (found)
    {
        *pValue = it->second;
    }

    return found;
}

string role::to_json(const Role& role)
{
    ostringstream ss;

    ss << "{"
       << "\"db\": \"" << role.db << "\", "
       << "\"role\": \"" << to_string(role.id) << "\""
       << "}";

    return ss.str();
}

bool role::from_json(const mxb::Json& json, Role* pRole)
{
    bool rv = false;

    if (json.type() == mxb::Json::Type::OBJECT)
    {
        string db;
        if (json.try_get_string("db", &db))
        {
            string role_name;
            if (json.try_get_string("role", &role_name))
            {
                Id id;
                rv = from_string(role_name, &id);

                if (rv)
                {
                    pRole->db = std::move(db);
                    pRole->id = id;
                }
            }
        }
    }

    return rv;
}

bool role::from_json(const string& s, Role* pRole)
{
    bool rv = false;

    mxb::Json json;

    if (json.load_string(s))
    {
        rv = from_json(json, pRole);
    }

    return rv;
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

bool role::from_json(const string& s, std::vector<role::Role>* pRoles)
{
    bool rv = false;

    mxb::Json json;

    if (json.load_string(s))
    {
        if (json.type() == mxb::Json::Type::ARRAY)
        {
            vector<Role> roles;

            auto elements = json.get_array_elems();

            rv = true;
            for (const auto& element : elements)
            {
                auto type = element.type();

                Role role;

                if (type == mxb::Json::Type::OBJECT)
                {
                    rv = from_json(element, &role);

                    if (!rv)
                    {
                        MXB_ERROR("'%s' is not a valid.role.",
                                  element.to_string(mxb::Json::Format::NORMAL).c_str());
                        break;
                    }
                }
                else
                {
                    MXB_ERROR("'%s' is a JSON array, but all elements are not objects.", s.c_str());
                    rv = false;
                    break;
                }

                roles.push_back(role);
            }

            if (rv)
            {
                pRoles->swap(roles);
            }
        }
        else
        {
            MXS_ERROR("'%s' is valid JSON, but not an array.", s.c_str());
        }
    }
    else
    {
        MXS_ERROR("'%s' is not valid JSON: %s", s.c_str(), json.error_msg().c_str());
    }

    return rv;
}

namespace
{

void add_role(role::Id role_id, const string& db, vector<role::Role>& roles)
{
    roles.push_back(role::Role { db, role_id });
}

void add_role(const string_view& role_name, const string& db, vector<role::Role>& roles)
{
    role::Id role_id;
    if (!role::from_string(role_name, &role_id))
    {
        ostringstream ss;
        ss << "No role named " << role_name << "@" << db;

        throw SoftError(ss.str(), error::ROLE_NOT_FOUND);
    }

    add_role(role_id, db, roles);
}

void add_role(const string_view& role_name, const string_view& db, vector<role::Role>& roles)
{
    add_role(role_name, string(db.data(), db.length()), roles);
}

void add_role(const bsoncxx::document::view& role_doc, vector<role::Role>& roles)
{
    auto e = role_doc[key::ROLE];
    if (!e)
    {
        throw SoftError("Missing expected field \"role\"", error::NO_SUCH_KEY);
    }

    if (e.type() != bsoncxx::type::k_utf8)
    {
        ostringstream ss;
        ss << "\"role\" had the wrong type. Expected string, found " << bsoncxx::to_string(e.type());
        throw SoftError(ss.str(), error::TYPE_MISMATCH);
    }

    string_view role_name = e.get_utf8();

    e = role_doc[key::DB];
    if (!e)
    {
        throw SoftError("Missing expected field \"db\"", error::NO_SUCH_KEY);
    }

    if (e.type() != bsoncxx::type::k_utf8)
    {
        ostringstream ss;
        ss << "\"db\" had the wrong type. Expected string, found " << bsoncxx::to_string(e.type());
        throw SoftError(ss.str(), error::TYPE_MISMATCH);
    }

    string_view db = e.get_utf8();

    add_role(role_name, db, roles);
}

}

void role::from_bson(const bsoncxx::array::view& bson,
                     const std::string& default_db,
                     std::vector<Role>* pRoles)
{
    vector<Role> roles;

    for (const auto& element : bson)
    {
        switch (element.type())
        {
        case bsoncxx::type::k_utf8:
            add_role(element.get_utf8(), default_db, roles);
            break;

        case bsoncxx::type::k_document:
            add_role(element.get_document(), roles);
            break;

        default:
            throw SoftError("Role names must be either strings or objects", error::BAD_VALUE);
        }
    }

    pRoles->swap(roles);
}

std::vector<uint8_t> UserManager::UserInfo::pwd_sha1() const
{
    return mxs::from_base64(this->pwd_sha1_b64);
}

vector<uint8_t> UserManager::UserInfo::salt_sha1() const
{
    return mxs::from_base64(this->salt_sha1_b64);
}

vector<uint8_t> UserManager::UserInfo::salt_sha256() const
{
    return mxs::from_base64(this->salt_sha256_b64);
}

vector<uint8_t> UserManager::UserInfo::salted_pwd_sha1() const
{
    return mxs::from_base64(this->salted_pwd_sha1_b64);
}

vector<uint8_t> UserManager::UserInfo::salted_pwd_sha256() const
{
    return mxs::from_base64(this->salted_pwd_sha256_b64);
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

namespace
{

bool is_accessible_by_others(const string& path)
{
    bool rv = false;

    struct stat s;
    if (stat(path.c_str(), &s) == 0)
    {
        mode_t permissions = s.st_mode & ACCESSPERMS;

        rv = (permissions & ~(S_IRWXU)) != 0;
    }

    return rv;
}

}

//static
unique_ptr<UserManager> UserManager::create(const string& name)
{
    nosql::UserManager* pThis = nullptr;

    string path = mxs::datadir();

    path += "/nosqlprotocol/";

    if (mxs_mkdir_all(path.c_str(), S_IRWXU))
    {
        // The directory is created on first start and at that point the permissions
        // should be right, but thereafter someone might change them.
        if (is_accessible_by_others(path))
        {
            MXS_ERROR("The directory '%s' is accessible by others. The nosqlprotocol "
                      "directory must only be accessible by MaxScale.",
                      path.c_str());
        }
        else
        {
            path += name;
            path += "-v";
            path += std::to_string(SCHEMA_VERSION);
            path += ".db";

            if (is_accessible_by_others(path))
            {
                MXS_ERROR("The file '%s' is accessible by others. The nosqlprotocol account "
                          "database must only be accessible by MaxScale.",
                          path.c_str());
            }
            else
            {
                sqlite3* pDb = open_or_create_db(path);

                if (pDb)
                {
                    // Ensure it is readable/writeable only by MaxScale. This should be
                    // necessary only when the database is created, but as you cannot
                    // provide a file mask to sqlite3, it's simpler to just do it always.
                    if (chmod(path.c_str(), S_IRUSR | S_IWUSR) == 0)
                    {
                        pThis = new UserManager(std::move(path), pDb);
                    }
                    else
                    {
                        MXS_ERROR("Could not make '%s' usable only by MaxScale: %s",
                                  path.c_str(), mxs_strerror(errno));

                        sqlite3_close_v2(pDb);
                        pDb = nullptr;
                    }
                }
                else
                {
                    // The handle will be null, *only* if the opening fails due to a memory
                    // allocation error.
                    MXS_ALERT("sqlite3 memory allocation failed, nosqlprotocol cannot continue.");
                }
            }
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

bool UserManager::add_user(const string& db,
                           string user,
                           string pwd,
                           const std::string& host,
                           const std::string& custom_data, // Assumed to be JSON document.
                           const vector<scram::Mechanism>& mechanisms,
                           const vector<role::Role>& roles)
{
    mxb_assert(custom_data.empty() || mxb::Json().load_string(custom_data));

    pwd = nosql::escape_essential_chars(pwd);

    string salt_sha1_b64;
    string salt_sha256_b64;
    string salted_pwd_sha1_b64;
    string salted_pwd_sha256_b64;

    for (const auto mechanism : mechanisms)
    {
        switch (mechanism)
        {
        case scram::Mechanism::SHA_1:
            {
                size_t hash_size = scram::ScramSHA1::HASH_SIZE;
                size_t salt_size = hash_size - 4; // To leave room for scram stuff.
                vector<uint8_t> salt = crypto::create_random_bytes(salt_size);
                salt_sha1_b64 = mxs::to_base64(salt);

                auto salted_pwd = scram::ScramSHA1::get().get_salted_password(user, pwd, salt);
                salted_pwd_sha1_b64 = mxs::to_base64(salted_pwd);
            }
            break;

        case scram::Mechanism::SHA_256:
            {
                size_t hash_size = scram::ScramSHA256::HASH_SIZE;
                size_t salt_size = hash_size - 4; // To leave room for scram stuff.
                vector<uint8_t> salt = crypto::create_random_bytes(salt_size);
                salt_sha256_b64 = mxs::to_base64(salt);

                auto salted_pwd = scram::ScramSHA256::get().get_salted_password(user, pwd, salt);
                salted_pwd_sha256_b64 = mxs::to_base64(salted_pwd);
            }
            break;

        default:
            mxb_assert(!true);
        }
    }

    user = nosql::escape_essential_chars(user);

    string mariadb_user = get_mariadb_user(db, user);

    vector<uint8_t> pwd_sha1 = crypto::sha_1(pwd);
    string pwd_sha1_b64 = mxs::to_base64(pwd_sha1);

    uuid_t uuid;
    uuid_generate(uuid);

    char zUuid[NOSQL_UUID_STR_LEN];
    uuid_unparse(uuid, zUuid);

    ostringstream ss;
    ss << SQL_INSERT_HEAD << "('"
       << mariadb_user << "', '"
       << db << "', '"
       << user << "', '"
       << pwd_sha1_b64 << "', '"
       << host << "', '"
       << custom_data << "', '"
       << zUuid << "', '"
       << salt_sha1_b64 << "', '"
       << salt_sha256_b64 << "', '"
       << salted_pwd_sha1_b64 << "', '"
       << salted_pwd_sha256_b64 << "', '"
       << role::to_json(roles)
       << "')";

    string sql = ss.str();

    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), nullptr, nullptr, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not add user '%s' to local database: %s",
                  mariadb_user.c_str(),
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return rv == SQLITE_OK;
}

bool UserManager::remove_user(const string& db, const string& user)
{
    string mariadb_user = get_mariadb_user(db, nosql::escape_essential_chars(user));

    ostringstream ss;
    ss << SQL_DELETE_WHERE_MARIADB_USER_HEAD << "\"" << mariadb_user << "\"";

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

bool UserManager::get_info(const string& db, const string& user, UserInfo* pInfo) const
{
    return get_info(get_mariadb_user(db, nosql::escape_essential_chars(user)), pInfo);
}

bool UserManager::get_info(const string& mariadb_user, UserInfo* pInfo) const
{
    ostringstream ss;
    ss << SQL_SELECT_WHERE_MARIADB_USER_HEAD << "\"" << mariadb_user << "\"";

    string sql = ss.str();

    vector<UserInfo> infos;
    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), select_info_cb, &infos, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not get data for user '%s' from local database: %s",
                  mariadb_user.c_str(),
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

vector<UserManager::UserInfo> UserManager::get_infos() const
{
    vector<UserInfo> infos;
    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, SQL_SELECT_ALL, select_info_cb, &infos, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not get user data from local database: %s",
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return infos;
}

vector<UserManager::UserInfo> UserManager::get_infos(const std::string& db) const
{
    ostringstream ss;
    ss << SQL_SELECT_WHERE_DB_HEAD << "\"" << db << "\"";

    string sql = ss.str();

    vector<UserInfo> infos;
    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), select_info_cb, &infos, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not get user data from local database: %s",
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return infos;
}

vector<UserManager::UserInfo> UserManager::get_infos(const vector<string>& mariadb_users) const
{
    vector<UserInfo> infos;

    if (!mariadb_users.empty())
    {
        ostringstream ss;
        ss << SQL_SELECT_WHERE_HEAD;

        auto it = mariadb_users.begin();
        for (; it != mariadb_users.end(); ++it)
        {
            if (it != mariadb_users.begin())
            {
                ss << " OR ";
            }

            ss << "mariadb_user = '" << *it << "'";
        }

        string sql = ss.str();

        char* pError = nullptr;
        int rv = sqlite3_exec(&m_db, sql.c_str(), select_info_cb, &infos, &pError);

        if (rv != SQLITE_OK)
        {
            MXS_ERROR("Could not get user data from local database: %s",
                      pError ? pError : "Unknown error");
            sqlite3_free(pError);
        }
    }

    return infos;
}

vector<UserManager::MariaDBAccount> UserManager::get_mariadb_accounts(const string& db) const
{
    vector<MariaDBAccount> mariadb_accounts;

    ostringstream ss;
    ss << SQL_SELECT_MARIADB_USER_HOST_WHERE_DB_HEAD << "'" << db << "'";

    string sql = ss.str();

    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), select_mariadb_accounts_cb, &mariadb_accounts, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not get user data from local database: %s",
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return mariadb_accounts;
}

bool UserManager::remove_mariadb_accounts(const std::vector<MariaDBAccount>& mariadb_accounts) const
{
    int rv = SQLITE_OK;

    if (!mariadb_accounts.empty())
    {
        ostringstream ss;

        ss << SQL_DELETE_WHERE_HEAD;

        auto it = mariadb_accounts.begin();
        for (; it != mariadb_accounts.end(); ++it)
        {
            if (it != mariadb_accounts.begin())
            {
                ss << " OR ";
            }

            ss << "mariadb_user = '" << it->user << "'";
        }

        auto sql = ss.str();

        char* pError = nullptr;
        rv = sqlite3_exec(&m_db, sql.c_str(), nullptr, nullptr, &pError);

        if (rv != SQLITE_OK)
        {
            MXS_ERROR("Could not remove data from local database: %s",
                      pError ? pError : "Unknown error");
            sqlite3_free(pError);
        }
    }

    return rv == SQLITE_OK;
}

bool UserManager::update(const string& db, const string& user, uint32_t what, const UserInfo& info) const
{
    mxb_assert((what & UserInfo::MASK) != 0);

    int rv = SQLITE_OK;

    string mariadb_user = get_mariadb_user(db, nosql::escape_essential_chars(user));

    ostringstream ss;

    ss << SQL_UPDATE_HEAD;
    string delimiter = "";

    if (what & UserInfo::CUSTOM_DATA)
    {
        ss << delimiter << "custom_data = '" << info.custom_data << "'";
        delimiter = ", ";
    }

    if (what & UserInfo::MECHANISMS)
    {
        auto begin = info.mechanisms.begin();
        auto end = info.mechanisms.end();

        if (std::find(begin, end, scram::Mechanism::SHA_1) == end)
        {
            ss << delimiter << "salt_sha1_b64 = '', salted_pwd_sha1_b64 = ''";
            delimiter = ", ";
        }

        if (std::find(begin, end, scram::Mechanism::SHA_256) == end)
        {
            ss << delimiter << "salt_sha256_b64 = '', salted_pwd_sha256_b64 = ''";
            delimiter = ", ";
        }
    }

    if (what & UserInfo::PWD)
    {
        ss << delimiter << "pwd_sha1_b64 = '" << info.pwd_sha1_b64 << "'";
        delimiter = ", ";
    }

    if (what & UserInfo::ROLES)
    {
        ss << delimiter << "roles = '" << role::to_json(info.roles) << "'";
        delimiter = ", ";
    }

    ss << SQL_UPDATE_TAIL << "'" << mariadb_user << "'";

    auto sql = ss.str();

    char* pError = nullptr;
    rv = sqlite3_exec(&m_db, sql.c_str(), nullptr, nullptr, &pError);

    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could update '%s': %s", mariadb_user.c_str(),
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return rv == SQLITE_OK;
}

}

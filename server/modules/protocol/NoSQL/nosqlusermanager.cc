/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqlusermanager.hh"
#include <sys/stat.h>
#include <uuid/uuid.h>
#include <unistd.h>
#include <map>
#include <maxsql/mariadb.hh>
#include <maxscale/paths.hh>
#include <maxscale/service.hh>
#include <maxscale/utils.hh>
#include "configuration.hh"
#include "nosqlkeys.hh"
#include <maxscale/secrets.hh>

using namespace std;

using Guard = lock_guard<mutex>;

namespace
{

using namespace nosql;

// Not all uuid/uuid.h appears to have UUID_STR_LEN defined.
const int NOSQL_UUID_STR_LEN = 37;

void get_sha1_salt_and_salted_password(const string& user,
                                       const string& pwd,
                                       string* pSalt_b64,
                                       string* pSalted_pwd_b64)
{
    size_t hash_size = scram::ScramSHA1::HASH_SIZE;
    size_t salt_size = hash_size - 4; // To leave room for scram stuff.
    vector<uint8_t> salt = crypto::create_random_bytes(salt_size);
    *pSalt_b64 = mxs::to_base64(salt);

    auto salted_pwd = scram::ScramSHA1::get().get_salted_password(user, pwd, salt);
    *pSalted_pwd_b64 = mxs::to_base64(salted_pwd);
}

void get_sha256_salt_and_salted_password(const string& user,
                                         const string& pwd,
                                         string* pSalt_b64,
                                         string* pSalted_pwd_b64)
{
    size_t hash_size = scram::ScramSHA256::HASH_SIZE;
    size_t salt_size = hash_size - 4; // To leave room for scram stuff.
    vector<uint8_t> salt = crypto::create_random_bytes(salt_size);
    *pSalt_b64 = mxs::to_base64(salt);

    auto salted_pwd = scram::ScramSHA256::get().get_salted_password(user, pwd, salt);
    *pSalted_pwd_b64 = mxs::to_base64(salted_pwd);
}

void get_salts_and_salted_passwords(const string& user,
                                    const string& pwd,
                                    const vector<scram::Mechanism>& mechanisms,
                                    string* pSalt_sha1_b64,
                                    string* pSalted_pwd_sha1_b64,
                                    string* pSalt_sha256_b64,
                                    string* pSalted_pwd_sha256_b64)
{
    for (const auto mechanism : mechanisms)
    {
        switch (mechanism)
        {
        case scram::Mechanism::SHA_1:
            get_sha1_salt_and_salted_password(user, pwd, pSalt_sha1_b64, pSalted_pwd_sha1_b64);
            break;

        case scram::Mechanism::SHA_256:
            get_sha256_salt_and_salted_password(user, pwd, pSalt_sha256_b64, pSalted_pwd_sha256_b64);
            break;

        default:
            mxb_assert(!true);
        }
    }
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

string role::to_json_string(const Role& role)
{
    ostringstream ss;

    ss << "{"
       << "\"db\": \"" << role.db << "\", "
       << "\"role\": \"" << to_string(role.id) << "\""
       << "}";

    return ss.str();
}

mxb::Json role::to_json_object(const Role& role)
{
    mxb::Json json;

    json.set_string("db", role.db);
    json.set_string("role", to_string(role.id));

    return json;
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
                    pRole->db = move(db);
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

string role::to_json_string(const vector<role::Role>& roles)
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

        ss << to_json_string(role);
    }

    ss << "]";

    return ss.str();
}

mxb::Json role::to_json_array(const vector<role::Role>& roles)
{
    mxb::Json json(mxb::Json::Type::ARRAY);

    auto it = roles.begin();
    for (; it != roles.end(); ++it)
    {
        const auto& role = *it;

        json.add_array_elem(to_json_object(role));
    }

    return json;
}

bool role::from_json(const mxb::Json& json, vector<role::Role>* pRoles)
{
    bool rv = false;

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
            MXB_ERROR("'%s' is a JSON array, but all elements are not objects.",
                      json.to_string(mxb::Json::Format::NORMAL).c_str());
            rv = false;
            break;
        }

        roles.push_back(role);
    }

    if (rv)
    {
        pRoles->swap(roles);
    }

    return rv;
}

bool role::from_json(const string& s, vector<role::Role>* pRoles)
{
    bool rv = false;

    mxb::Json json;

    if (json.load_string(s))
    {
        if (json.type() == mxb::Json::Type::ARRAY)
        {
            rv = from_json(json, pRoles);
        }
        else
        {
            MXB_ERROR("'%s' is valid JSON, but not an array.", s.c_str());
        }
    }
    else
    {
        MXB_ERROR("'%s' is not valid JSON: %s", s.c_str(), json.error_msg().c_str());
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
                     const string& default_db,
                     vector<Role>* pRoles)
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

namespace
{

bool contains(const set<string>& heystack, const set<string>& needles)
{
    set<string> found;
    std::set_intersection(heystack.begin(), heystack.end(),
                          needles.begin(), needles.end(),
                          std::inserter(found, found.begin()));

    return found == needles;
}

}

namespace
{

vector<role::Role> from_grant(bool is_admin,
                              bool has_all,
                              bool is_any,
                              string db,
                              const set<string>& priv_types,
                              string on,
                              bool with_grant_option)
{
    vector<role::Role> roles;
    set<string> required;

    // DB_ADMIN, DB_ADMIN_ANY_DATABASE
    bool has_dbAdmin = false;
    required = {"ALTER", "CREATE", "DROP", "SELECT"};

    if (is_admin)
    {
        required.insert("SHOW DATABASES");
    }

    if (has_all || contains(priv_types, required))
    {
        roles.push_back({db, is_any ? role::Id::DB_ADMIN_ANY_DATABASE : role::Id::DB_ADMIN});
        has_dbAdmin = true;
    }

    // READ_WRITE, READ_WRITE_ANY_DATABASE
    bool has_readWrite = false;
    required = {"CREATE", "DELETE", "INDEX", "INSERT", "SELECT", "UPDATE"};

    if (has_all || contains(priv_types, required))
    {
        roles.push_back({db, is_any ? role::Id::READ_WRITE_ANY_DATABASE : role::Id::READ_WRITE});
        has_readWrite = true;
    }

    if (!has_readWrite)
    {
        // READ, READ_ANY_DATABASE
        required = {"SELECT"};

        if (has_all || contains(priv_types, required))
        {
            roles.push_back({db, is_any ? role::Id::READ_ANY_DATABASE : role::Id::READ});
        }
    }

    // USER_ADMIN, USER_ADMIN_ANY_DATABASE
    bool has_userAdmin = false;

    if (with_grant_option)
    {
        roles.push_back({db, is_any ? role::Id::USER_ADMIN_ANY_DATABASE : role::Id::USER_ADMIN });
        has_userAdmin = true;
    }

    // DB_OWNER, ROOT
    if (has_dbAdmin && has_readWrite && has_userAdmin)
    {
        roles.push_back({db, is_admin ? role::Id::ROOT : role::Id::DB_OWNER });
    }

    return roles;
}

}

bool role::from_grant(bool is_admin,
                      const set<string>& priv_types,
                      string on,
                      bool with_grant_option,
                      vector<role::Role>* pRoles)
{
    bool rv = true;

    bool is_any = (on == "*.*");

    if (is_any && (priv_types.size() == 1 && priv_types.count("USAGE") == 1))
    {
        // 'ON *.*' is accepted if "USAGE" is alone. That basically
        // only tells the user exists.
        pRoles->clear();
        rv = true;
    }
    else if (is_any && !is_admin)
    {
        MXB_ERROR("A grant ON *.* can only be assigned to a user in the 'admin' database.");
        rv = false;
    }
    else
    {
        vector<role::Role> roles;

        bool has_all = (priv_types.count("ALL PRIVILEGES") != 0);
        string db;

        if (is_admin)
        {
            db = "admin";
        }
        else
        {
            bool back_tick = (on.front() == '`');
            int b = back_tick ? 1 : 0;
            int e;
            int t; // Position of table name.

            if (back_tick)
            {
                e = on.find('`', b);
                t = e + 2;
            }
            else
            {
                e = on.find(b, '.');
                t = e + 1;
            }

            db = on.substr(b, e - b);

            auto table = on.substr(t);

            if (table != "*")
            {
                MXB_ERROR("Grants must be ON generic `%s`.* and not ON a specific table `%s`.%s.",
                          db.c_str(), db.c_str(), table.c_str());
                rv = false;
            }
        }

        if (rv)
        {
            if (is_admin && (on != "*.*"))
            {
                MXB_ERROR("Grants for admin users must be ON *.*, not ON e.g. %s.", on.c_str());
                rv = false;
            }
            else
            {
                *pRoles = ::from_grant(is_admin, has_all, is_any, db, priv_types, on, with_grant_option);
            }
        }
    }

    return rv;
}

bool role::get_grant_characteristics(string grant,
                                     set<string>* pPriv_types,
                                     string* pOn,
                                     bool* pWith_grant_option)
{
    bool rv = false;

    if (grant.find("GRANT ") == 0)
    {
        grant = grant.substr(6); // strlen("GRANT ");

        auto i = grant.find(" ON ");

        if (i != string::npos)
        {
            auto priv_types_string = grant.substr(0, i);
            grant = grant.substr(i + 4); // strlen(" ON ");

            auto j = grant.find(" TO ");

            if (j != string::npos)
            {
                auto on = grant.substr(0, j);
                grant = grant.substr(j + 4); // strlen(" TO ");

                vector<string> tmp = mxb::strtok(priv_types_string, ",");
                set<string> priv_types;

                std::for_each(tmp.begin(), tmp.end(), [&priv_types](string s) {
                        mxb::trim(s);
                        priv_types.insert(s);
                    });

                *pPriv_types = std::move(priv_types);
                *pOn = std::move(on);
                *pWith_grant_option = (grant.find("WITH GRANT OPTION") != string::npos);

                rv = true;
            }
        }
    }

    return rv;
}


/**
 * UserManager
 */
vector<uint8_t> UserManager::UserInfo::pwd_sha1() const
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

UserManager::~UserManager()
{
    cancel_dcalls();
}

UserManager::AddUser UserManager::get_add_user_data(const string& db,
                                                    string user,
                                                    string pwd,
                                                    const string& host,
                                                    const vector<scram::Mechanism>& mechanisms)
{
    pwd = nosql::escape_essential_chars(pwd);

    string salt_sha1_b64;
    string salted_pwd_sha1_b64;
    string salt_sha256_b64;
    string salted_pwd_sha256_b64;

    get_salts_and_salted_passwords(user, pwd,
                                   mechanisms,
                                   &salt_sha1_b64, &salted_pwd_sha1_b64,
                                   &salt_sha256_b64, &salted_pwd_sha256_b64);

    user = nosql::escape_essential_chars(user);

    string mariadb_user = get_mariadb_user(db, user);

    vector<uint8_t> pwd_sha1 = crypto::sha_1(pwd);
    string pwd_sha1_b64 = mxs::to_base64(pwd_sha1);

    uuid_t uuid;
    uuid_generate(uuid);

    char zUuid[NOSQL_UUID_STR_LEN];
    uuid_unparse(uuid, zUuid);

    AddUser rv;

    rv.mariadb_user = mariadb_user;
    rv.db = db;
    rv.user = user;
    rv.pwd = pwd;
    rv.host = host;
    rv.salt_sha1_b64 = salt_sha1_b64;
    rv.salted_pwd_sha1_b64 = salted_pwd_sha1_b64;
    rv.salt_sha256_b64 = salt_sha256_b64;
    rv.salted_pwd_sha256_b64 = salted_pwd_sha256_b64;
    rv.pwd_sha1_b64 = pwd_sha1_b64;
    rv.uuid = zUuid;

    return rv;
}

void UserManager::ensure_initial_user()
{
    mxb_assert(mxs::MainWorker::is_current());

    const SERVER* pMaster = get_master();

    if (pMaster)
    {
        check_initial_user(pMaster);
    }
    else
    {
        MXB_INFO("Primary not yet available, checking shortly again.");

        dcall(1s, [this]() {
                const SERVER* pM = get_master();

                if (pM)
                {
                    check_initial_user(pM);
                }
                else
                {
                    MXB_INFO("Primary still not available, checking shortly again.");
                }

                return !pM;
            });
    }
}

const SERVER* UserManager::get_master() const
{
    const SERVER* pMaster = nullptr;
    auto servers = m_service.reachable_servers();

    for (const auto* pServer : servers)
    {
        if (status_is_master(pServer->status()))
        {
            pMaster = pServer;
            break;
        }
    }

    return pMaster;
}

void UserManager::check_initial_user(const SERVER* pMaster)
{
    auto infos = get_infos();

    if (infos.empty())
    {
        MXB_INFO("No existing NoSQL user. Assuming first startup, creating initial user.");

        if (!m_config.user.empty())
        {
            create_initial_user(pMaster);
        }
        else
        {
            MXB_ERROR("Initial NoSQL user should be created, but 'user' is empty. Cannot create user.");
        }
    }
    else
    {
        MXB_INFO("At least one NoSQL user exists, no need to create one.");
    }
}

void UserManager::create_initial_user(const SERVER* pMaster)
{
    MYSQL* pMysql = mysql_init(nullptr);

    if (pMaster->proxy_protocol())
    {
        mxq::set_proxy_header(pMysql);
    }

    if (mysql_real_connect(pMysql, pMaster->address(),
                           m_config.user.c_str(), m_config.password.c_str(),
                           nullptr, pMaster->port(), nullptr, 0))
    {
        bool grants_obtained = true;
        vector<string> grants;

        if (mysql_query(pMysql, "SHOW GRANTS") == 0)
        {
            mxb_assert(mysql_field_count(pMysql) == 1);

            MYSQL_RES* pResult = mysql_store_result(pMysql);

            while (MYSQL_ROW row = mysql_fetch_row(pResult))
            {
                grants.push_back(row[0]);
            }

            mysql_free_result(pResult);
        }
        else
        {
            MXB_ERROR("SHOW GRANTS for '%s' failed: %s",
                      m_config.user.c_str(), mysql_error(pMysql));
            grants_obtained = false;
        }

        if (grants_obtained)
        {
            create_initial_user(grants);
        }
    }
    else
    {
        MXB_ERROR("Could not connect to %s:%d as %s. Cannot create initial NoSQL user.",
                  pMaster->address(), (int)pMaster->port(), m_config.user.c_str());
    }

    mysql_close(pMysql);
}

void UserManager::create_initial_user(const vector<string>& grants)
{
    vector<role::Role> roles;
    bool is_admin = m_config.user.find("admin.") == 0;

    bool converted = true;
    for (auto grant : grants)
    {
        bool success = true;

        set<string> priv_types;
        string on;
        bool with_grant_option;

        if (role::get_grant_characteristics(grant, &priv_types, &on, &with_grant_option))
        {
            vector<role::Role> some_roles;
            if (role::from_grant(is_admin, priv_types, on, with_grant_option, &some_roles))
            {
                roles.insert(roles.end(),
                             std::move_iterator(some_roles.begin()), std::move_iterator(some_roles.end()));
            }
            else
            {
                MXB_ERROR("Could not convert '%s' into equivalent NoSQL roles. See above for more details.",
                          grant.c_str());
                converted = false;
            }
        }
        else
        {
            MXB_ERROR("SHOW GRANTS returned '%s', which does not look like a GRANT.", grant.c_str());
            converted = false;
        }

        if (!converted)
        {
            break;
        }
    }

    if (converted)
    {
        // As NoSQL users are specific to a certain database, a convention for
        // dealing with that is needed.
        //
        // - If the user looks like "db.bob", a NoSQL user "bob" will be created
        //   in the NoSQL database "db".
        // - If the user looks like "bob", a NoSQL user "bob" will be created
        //   in the NoSQL database "mariadb".
        //
        // The "mariadb" database is specific in the sense, that when the NoSQL
        // user "bob" authenticates in the context of the NoSQL database "mariadb",
        // then the user name, i.e. "bob", will not be prefixed by the database
        // name, i.e. "mariadb", when authenticating against MariaDB.
        //
        // The purpose of the NoSQL database "mariadb" is to make convenient usage
        // with the same user from both MariaDB and NoSQL possible.

        auto i = m_config.user.find(".");

        string db = (i != string::npos ? m_config.user.substr(0, i) : "mariadb");
        string user = (i != string::npos ? m_config.user.substr(i + 1) : m_config.user);

        vector<scram::Mechanism> mechanisms = { scram::Mechanism::SHA_256 };
        if (add_user(db, user, m_config.password, m_config.host, "", mechanisms, roles))
        {
            MXB_NOTICE("Created initial NoSQL user '%s.%s'.", db.c_str(), user.c_str());
        }
        else
        {
            MXB_ERROR("Could not create default NoSQL user '%s.%s'.", db.c_str(), user.c_str());
        }
    }
    else
    {
        MXB_ERROR("The grants of %s could not be converted into equivalent NoSQL roles. "
                  "Initial NoSQL user could not be created.", m_config.user.c_str());
    }
}

/**
 * UserManagerSqlite3
 */

namespace
{

constexpr int SQLITE3_SCHEMA_VERSION = 1;

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

static const char SQL_SELECT_ACCOUNT_INFO_WHERE_DB_HEAD[] =
    "SELECT mariadb_user, user, db, host FROM accounts WHERE db = ";

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
        MXB_ERROR("The 'roles' value of '%s' is not valid.", info.mariadb_user.c_str());
        ok = false;
    }

    if (!ok)
    {
        MXB_WARNING("Ignoring user '%s'.", info.mariadb_user.c_str());
    }

    return 0;
}

int select_account_info_cb(void* pData, int nColumns, char** pzColumn, char** pzNames)
{
    mxb_assert(nColumns == 4);

    using Account = nosql::UserManager::Account;
    auto* pAccounts = static_cast<vector<Account>*>(pData);

    pAccounts->emplace_back(Account { pzColumn[0], pzColumn[1], pzColumn[2], pzColumn[3] });

    return 0;
}

bool create_schema(sqlite3* pDb)
{
    char* pError = nullptr;
    int rv = sqlite3_exec(pDb, SQL_CREATE, nullptr, nullptr, &pError);

    if (rv != SQLITE_OK)
    {
        MXB_ERROR("Could not initialize sqlite3 database: %s", pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return rv == SQLITE_OK;
}

sqlite3* open_or_create_db(const string& path)
{
    sqlite3* pDb = nullptr;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_CREATE;
    int rv = sqlite3_open_v2(path.c_str(), &pDb, flags, nullptr);

    if (rv == SQLITE_OK)
    {
        if (create_schema(pDb))
        {
            MXB_NOTICE("sqlite3 database %s open/created and initialized.", path.c_str());
        }
        else
        {
            MXB_ERROR("Could not create schema in sqlite3 database %s.", path.c_str());

            if (unlink(path.c_str()) != 0)
            {
                MXB_ERROR("Failed to delete database %s that could not be properly "
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
            MXB_ERROR("Opening/creating the sqlite3 database %s failed: %s",
                      path.c_str(), sqlite3_errmsg(pDb));
        }
        MXB_ERROR("Could not open sqlite3 database for storing user information.");
    }

    return pDb;
}

}

UserManagerSqlite3::UserManagerSqlite3(string path,
                                       sqlite3* pDb,
                                       SERVICE* pService,
                                       const Configuration* pConfig)
    : UserManager(pService, pConfig)
    , m_path(std::move(path))
    , m_db(*pDb)
{
}

UserManagerSqlite3::~UserManagerSqlite3()
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
unique_ptr<UserManager> UserManagerSqlite3::create(const string& name,
                                                   SERVICE* pService,
                                                   const Configuration* pConfig)
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
            MXB_ERROR("The directory '%s' is accessible by others. The nosqlprotocol "
                      "directory must only be accessible by MaxScale.",
                      path.c_str());
        }
        else
        {
            path += name;
            path += "-v";
            path += std::to_string(SQLITE3_SCHEMA_VERSION);
            path += ".db";

            if (is_accessible_by_others(path))
            {
                MXB_ERROR("The file '%s' is accessible by others. The nosqlprotocol account "
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
                        pThis = new UserManagerSqlite3(std::move(path), pDb, pService, pConfig);
                    }
                    else
                    {
                        MXB_ERROR("Could not make '%s' usable only by MaxScale: %s",
                                  path.c_str(), mxb_strerror(errno));

                        sqlite3_close_v2(pDb);
                        pDb = nullptr;
                    }
                }
                else
                {
                    // The handle will be null, *only* if the opening fails due to a memory
                    // allocation error.
                    MXB_ALERT("sqlite3 memory allocation failed, nosqlprotocol cannot continue.");
                }
            }
        }
    }
    else
    {
        MXB_ERROR("Could not create the directory %s, needed by the listener %s "
                  "for storing nosqlprotocol user information.",
                  path.c_str(), name.c_str());
    }

    return unique_ptr<UserManager>(pThis);
}

bool UserManagerSqlite3::add_user(const string& db,
                                  string user,
                                  string pwd,
                                  const string& host,
                                  const string& custom_data, // Assumed to be JSON document.
                                  const vector<scram::Mechanism>& mechanisms,
                                  const vector<role::Role>& roles)
{
    mxb_assert(custom_data.empty() || mxb::Json().load_string(custom_data));

    AddUser au = get_add_user_data(db, user, pwd, host, mechanisms);

    ostringstream ss;
    ss << SQL_INSERT_HEAD << "('"
       << au.mariadb_user << "', '"
       << au.db << "', '"
       << au.user << "', '"
       << au.pwd_sha1_b64 << "', '"
       << au.host << "', '"
       << custom_data << "', '"
       << au.uuid << "', '"
       << au.salt_sha1_b64 << "', '"
       << au.salt_sha256_b64 << "', '"
       << au.salted_pwd_sha1_b64 << "', '"
       << au.salted_pwd_sha256_b64 << "', '"
       << role::to_json_string(roles)
       << "')";

    string sql = ss.str();

    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), nullptr, nullptr, &pError);

    if (rv != SQLITE_OK)
    {
        MXB_ERROR("Could not add user '%s' to local database: %s",
                  au.mariadb_user.c_str(),
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return rv == SQLITE_OK;
}

bool UserManagerSqlite3::remove_user(const string& db, const string& user)
{
    string mariadb_user = get_mariadb_user(db, nosql::escape_essential_chars(user));

    ostringstream ss;
    ss << SQL_DELETE_WHERE_MARIADB_USER_HEAD << "\"" << mariadb_user << "\"";

    string sql = ss.str();

    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), nullptr, nullptr, &pError);

    if (rv != SQLITE_OK)
    {
        MXB_ERROR("Could not remove user '%s' from local database: %s",
                  user.c_str(),
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return rv == SQLITE_OK;
}

bool UserManagerSqlite3::get_info(const string& mariadb_user, UserInfo* pInfo) const
{
    ostringstream ss;
    ss << SQL_SELECT_WHERE_MARIADB_USER_HEAD << "\"" << mariadb_user << "\"";

    string sql = ss.str();

    vector<UserInfo> infos;
    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), select_info_cb, &infos, &pError);

    if (rv != SQLITE_OK)
    {
        MXB_ERROR("Could not get data for user '%s' from local database: %s",
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

vector<UserManager::UserInfo> UserManagerSqlite3::get_infos() const
{
    vector<UserInfo> infos;
    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, SQL_SELECT_ALL, select_info_cb, &infos, &pError);

    if (rv != SQLITE_OK)
    {
        MXB_ERROR("Could not get user data from local database: %s",
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return infos;
}

vector<UserManager::UserInfo> UserManagerSqlite3::get_infos(const string& db) const
{
    ostringstream ss;
    ss << SQL_SELECT_WHERE_DB_HEAD << "\"" << db << "\"";

    string sql = ss.str();

    vector<UserInfo> infos;
    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), select_info_cb, &infos, &pError);

    if (rv != SQLITE_OK)
    {
        MXB_ERROR("Could not get user data from local database: %s",
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return infos;
}

vector<UserManager::UserInfo> UserManagerSqlite3::get_infos(const vector<string>& mariadb_users) const
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
            MXB_ERROR("Could not get user data from local database: %s",
                      pError ? pError : "Unknown error");
            sqlite3_free(pError);
        }
    }

    return infos;
}

vector<UserManager::Account> UserManagerSqlite3::get_accounts(const string& db) const
{
    vector<Account> mariadb_accounts;

    ostringstream ss;
    ss << SQL_SELECT_ACCOUNT_INFO_WHERE_DB_HEAD << "'" << db << "'";

    string sql = ss.str();

    char* pError = nullptr;
    int rv = sqlite3_exec(&m_db, sql.c_str(), select_account_info_cb, &mariadb_accounts, &pError);

    if (rv != SQLITE_OK)
    {
        MXB_ERROR("Could not get user data from local database: %s",
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return mariadb_accounts;
}

bool UserManagerSqlite3::remove_accounts(const vector<Account>& accounts) const
{
    int rv = SQLITE_OK;

    if (!accounts.empty())
    {
        ostringstream ss;

        ss << SQL_DELETE_WHERE_HEAD;

        auto it = accounts.begin();
        for (; it != accounts.end(); ++it)
        {
            if (it != accounts.begin())
            {
                ss << " OR ";
            }

            ss << "mariadb_user = '" << it->mariadb_user << "'";
        }

        auto sql = ss.str();

        char* pError = nullptr;
        rv = sqlite3_exec(&m_db, sql.c_str(), nullptr, nullptr, &pError);

        if (rv != SQLITE_OK)
        {
            MXB_ERROR("Could not remove data from local database: %s",
                      pError ? pError : "Unknown error");
            sqlite3_free(pError);
        }
    }

    return rv == SQLITE_OK;
}

bool UserManagerSqlite3::update(const string& db, const string& user, uint32_t what, const Update& data) const
{
    mxb_assert((what & Update::MASK) != 0);

    int rv = SQLITE_OK;

    string mariadb_user = get_mariadb_user(db, nosql::escape_essential_chars(user));

    ostringstream ss;

    ss << SQL_UPDATE_HEAD;
    string delimiter = "";

    if (what & Update::CUSTOM_DATA)
    {
        ss << delimiter << "custom_data = '" << data.custom_data << "'";
        delimiter = ", ";
    }

    if (what & Update::PWD)
    {
        auto pwd = nosql::escape_essential_chars(data.pwd);
        vector<uint8_t> pwd_sha1 = crypto::sha_1(pwd);
        string pwd_sha1_b64 = mxs::to_base64(pwd_sha1);

        ss << delimiter << "pwd_sha1_b64 = '" << pwd_sha1_b64 << "'";

        string salt_sha1_b64;
        string salted_pwd_sha1_b64;
        string salt_sha256_b64;
        string salted_pwd_sha256_b64;

        mxb_assert(!data.mechanisms.empty());
        get_salts_and_salted_passwords(user, pwd,
                                       data.mechanisms,
                                       &salt_sha1_b64, &salted_pwd_sha1_b64,
                                       &salt_sha256_b64, &salted_pwd_sha256_b64);

        ss << ", salt_sha1_b64 = '" << salt_sha1_b64 << "'"
           << ", salt_sha256_b64 = '" << salt_sha256_b64 << "'"
           << ", salted_pwd_sha1_b64 = '" << salted_pwd_sha1_b64 << "'"
           << ", salted_pwd_sha256_b64 = '" << salted_pwd_sha256_b64 << "'";

        delimiter = ", ";
    }

    if ((what & Update::MECHANISMS) && !(what & Update::PWD))
    {
        auto begin = data.mechanisms.begin();
        auto end = data.mechanisms.end();

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

    if (what & Update::ROLES)
    {
        ss << delimiter << "roles = '" << role::to_json_string(data.roles) << "'";
        delimiter = ", ";
    }

    ss << SQL_UPDATE_TAIL << "'" << mariadb_user << "'";

    auto sql = ss.str();

    char* pError = nullptr;
    rv = sqlite3_exec(&m_db, sql.c_str(), nullptr, nullptr, &pError);

    if (rv != SQLITE_OK)
    {
        MXB_ERROR("Could update '%s': %s", mariadb_user.c_str(),
                  pError ? pError : "Unknown error");
        sqlite3_free(pError);
    }

    return rv == SQLITE_OK;
}

/**
 * UserManagerMariaDB
 */

constexpr char MARIADB_ENCRYPTION_VERSION_DELIMITER[] = ":";

UserManagerMariaDB::UserManagerMariaDB(string name, SERVICE* pService, const Configuration* pConfig)
    : UserManager(pService, pConfig)
    , m_name(name)
    , m_table("`" + pConfig->authentication_db + "`.`" + m_name + "`")
{
    auto& settings = m_db.connection_settings();

    settings.user = m_config.authentication_user;
    settings.password = m_config.authentication_password;
}

//static
unique_ptr<UserManager> UserManagerMariaDB::create(string name,
                                                   SERVICE* pService,
                                                   const Configuration* pConfig)
{
    return unique_ptr<UserManager>(new UserManagerMariaDB(name, pService, pConfig));
}

bool UserManagerMariaDB::add_user(const string& db,
                                  string user,
                                  string pwd, // Cleartext
                                  const string& host,
                                  const string& custom_data, // Assumed to be JSON document.
                                  const vector<scram::Mechanism>& mechanisms,
                                  const vector<role::Role>& roles)
{
    bool rv = false;

    Guard guard(m_mutex);
    if (check_connection())
    {
        rv = do_add_user(db, user, pwd, host, custom_data, mechanisms, roles);
    }

    return rv;
}

bool UserManagerMariaDB::remove_user(const string& db, const string& user)
{
    bool rv = false;

    Guard guard(m_mutex);
    if (check_connection())
    {
        rv = do_remove_user(db, user);
    }

    return rv;
}

bool UserManagerMariaDB::get_info(const string& mariadb_user, UserInfo* pInfo) const
{
    bool rv = false;

    Guard guard(m_mutex);
    if (check_connection())
    {
        rv = do_get_info(mariadb_user, pInfo);
    }

    return rv;
}

vector<UserManager::UserInfo> UserManagerMariaDB::get_infos() const
{
    vector<UserInfo> rv;

    Guard guard(m_mutex);
    if (check_connection())
    {
        rv = do_get_infos();
    }

    return rv;
}

vector<UserManager::UserInfo> UserManagerMariaDB::get_infos(const string& db) const
{
    vector<UserInfo> rv;

    Guard guard(m_mutex);
    if (check_connection())
    {
        rv = do_get_infos(db);
    }

    return rv;
}

vector<UserManager::UserInfo> UserManagerMariaDB::get_infos(const vector<string>& mariadb_users) const
{
    vector<UserInfo> rv;

    Guard guard(m_mutex);
    if (check_connection())
    {
        rv = do_get_infos(mariadb_users);
    }

    return rv;
}

vector<UserManager::Account> UserManagerMariaDB::get_accounts(const string& db) const
{
    vector<Account> rv;

    Guard guard(m_mutex);
    if (check_connection())
    {
        rv = do_get_accounts(db);
    }

    return rv;
}

bool UserManagerMariaDB::remove_accounts(const vector<Account>& accounts) const
{
    bool rv = false;

    Guard guard(m_mutex);
    if (check_connection())
    {
        rv = do_remove_accounts(accounts);
    }

    return rv;
}

bool UserManagerMariaDB::update(const string& db,
                                const string& user,
                                uint32_t what,
                                const Update& data) const
{
    bool rv = false;

    Guard guard(m_mutex);
    if (check_connection())
    {
        rv = do_update(db, user, what, data);
    }

    return rv;
}

bool UserManagerMariaDB::check_connection() const
{
    if (m_db.is_open() && !status_is_master(m_pServer->status()))
    {
        m_db.close();
        m_pServer = nullptr;
    }

    if (!m_db.is_open())
    {
        SERVER* pServer = nullptr;
        auto servers = m_service.reachable_servers(); // Returns a value.

        auto end = servers.end();
        auto it = std::find_if(servers.begin(), end, std::mem_fun(&SERVER::is_master));

        if (it != end)
        {
            SERVER* pSrv = *it;

            MXB_SINFO(m_name << " uses " << pSrv->name() << " for storing user data.");

            if (m_db.open(pSrv->address(), pSrv->port()))
            {
                if (prepare_server())
                {
                    m_pServer = pSrv;
                }
                else
                {
                    m_db.close();
                }
            }
            else
            {
                MXB_SERROR("Could not open connection to " << pSrv->name() << " at "
                           << pSrv->address() << ":" << pSrv->port() << ": " << m_db.error());
            }
        }
        else
        {
            MXB_SWARNING("No primary server currently available for " << m_name << ", users cannot be "
                         "authenticated.");
        }
    }

    mxb_assert((m_db.is_open() && m_pServer) || (!m_db.is_open() && !m_pServer));

    return m_db.is_open();
}

bool UserManagerMariaDB::prepare_server() const
{
    bool rv = false;

    mxb_assert(m_db.is_open());

    string db = "`" + m_config.authentication_db + "`";

    ostringstream sql;
    sql << "CREATE DATABASE IF NOT EXISTS " << db;

    if (m_db.cmd(sql.str()))
    {
        MXB_SINFO("Created database " << db << ".");

        sql.str(string());
        sql << "CREATE TABLE IF NOT EXISTS " << m_table
            << " (mariadb_user VARCHAR(145) UNIQUE, db VARCHAR(64), user VARCHAR(80), "
            << "  host VARCHAR(60), data TEXT)";

        if (m_db.cmd(sql.str()))
        {
            MXB_SINFO("Created table " << m_table << ".");
            rv = true;
        }
        else
        {
            MXB_SERROR("Could not create table " << m_table << ": " << m_db.error());
        }
    }
    else
    {
        MXB_SERROR("Could not create database " << m_config.authentication_db << ": " << m_db.error());
    }

    return rv;
}

string UserManagerMariaDB::encrypt_data(const mxb::Json& json, const std::string& mariadb_user) const
{
    string data = json.to_string(mxb::Json::Format::NORMAL);

    if (!m_config.encryption_key.empty())
    {
        if (auto km = mxs::key_manager())
        {
            if (auto [ok, vers, key] = km->get_key(m_config.authentication_key_id); ok)
            {
                data = mxs::encrypt_password(key, data);

                if (!data.empty())
                {
                    data = std::to_string(vers) + MARIADB_ENCRYPTION_VERSION_DELIMITER + data;
                }
                else
                {
                    MXB_ERROR("Could not encrypt NoSQL data, cannot create/update user '%s'.",
                              mariadb_user.c_str());
                }
            }
            else
            {
                MXB_ERROR("Could not retrieve encryption key, cannot create/update user '%s'.",
                          mariadb_user.c_str());
                data.clear();
            }
        }
    }

    return data;
}

namespace
{

string::size_type get_encryption_version(const std::string& data,
                                         const std::string& mariadb_user,
                                         int* pVersion)
{
    auto i = data.find(MARIADB_ENCRYPTION_VERSION_DELIMITER);

    if (i != string::npos)
    {
        string version = data.substr(0, i);

        char* zEnd;
        long l = strtol(version.c_str(), &zEnd, 10);

        if (*zEnd == 0)
        {
            *pVersion = l;
            i++;
        }
        else
        {
            // Wasn't a number, so can't be encrypted data of correct format, probably just JSON.
            i = string::npos;
        }
    }
    else
    {
        // If encrypted, there should be the "N:" prefix. If not, a non-empty JSON
        // object will contain a ":".
        MXB_SERROR("The data of '" << mariadb_user << "' does not appear to be valid.");
    }

    return i;
}

}

string UserManagerMariaDB::decrypt_data(std::string data, const std::string& mariadb_user) const
{
    int version = -1;
    auto i = get_encryption_version(data, mariadb_user, &version);

    if (!m_config.encryption_key.empty())
    {
        // Encryption enabled
        if (i != string::npos)
        {
            if ((uint32_t)version == m_config.encryption_key_version)
            {
                // Same version as the current encryption key
                data = mxs::decrypt_password(m_config.encryption_key, data.substr(i));
            }
            else if (auto km = mxs::key_manager())
            {
                if (auto [ok, vers, key] = km->get_key(m_config.authentication_key_id, version); ok)
                {
                    if (key.size() == mxs::SECRETS_CIPHER_BYTES)
                    {
                        data = mxs::decrypt_password(key, data.substr(i));
                    }
                    else
                    {
                        MXB_SERROR("Encryption key version '" << version << "' is not a 256-bit key.");
                        data.clear();
                    }
                }
                else
                {
                    MXB_ERROR("The version '%d' of the encrypted data of '%s' is "
                              "unknown or the key retrieval failed. User will be ignored.",
                              version, mariadb_user.c_str());
                    data.clear();
                }
            }
            else
            {
                MXB_ERROR("Found encrypted password but encryption key manager is no longer enabled");
                data.clear();
                mxb_assert_message(!true, "KeyManager should not be disabled if encryption_key is not empty");
            }
        }
        else
        {
            // Probably is JSON and if it isn't, it'll be found out when the data is later loaded as such.
            MXB_SINFO("The data of '" << mariadb_user << "' lacks an encryption version prefix. "
                      << "Assuming the data was saved when encryption was not enabled.");
        }
    }
    else
    {
        // Encryption NOT enabled
        if (i != string::npos)
        {
            MXB_WARNING("The data of '%s' appears to be encrypted, but 'key_manager' has not been enabled. "
                        "The subsequent parsing of it as JSON will fail.", mariadb_user.c_str());
        }
    }

    return data;
}

bool UserManagerMariaDB::do_add_user(const string& db,
                                     string user,
                                     string pwd, // Cleartext
                                     const string& host,
                                     const string& custom_data_json, // Assumed to be JSON document.
                                     const vector<scram::Mechanism>& mechanisms,
                                     const vector<role::Role>& roles)
{
    bool rv = true;

    AddUser au = get_add_user_data(db, user, pwd, host, mechanisms);

    mxb::Json json;

    json.set_string("salt_sha1_b64", au.salt_sha1_b64);
    json.set_string("salted_pwd_sha1_b64", au.salted_pwd_sha1_b64);
    json.set_string("salt_sha256_b64", au.salt_sha256_b64);
    json.set_string("salted_pwd_sha256_b64", au.salted_pwd_sha256_b64);
    json.set_string("pwd_sha1_b64", au.pwd_sha1_b64);
    json.set_string("uuid", au.uuid);
    if (!custom_data_json.empty())
    {
        mxb::Json custom_data(mxb::Json::Type::OBJECT);
        MXB_AT_DEBUG(bool loaded =) custom_data.load_string(custom_data_json);
        mxb_assert(loaded);
        json.set_object("custom_data", std::move(custom_data));
    }
    json.set_object("roles", role::to_json_array(roles));

    string data = encrypt_data(json, au.mariadb_user);
    rv = !data.empty();

    if (rv)
    {
        ostringstream ss;
        ss << "INSERT INTO " << m_table << " VALUES ("
           << "'" << au.mariadb_user << "', "
           << "'" << au.db << "', "
           << "'" << au.user << "', "
           << "'" << au.host << "', "
           << "'" << data << "'"
           << ")";

        rv = m_db.cmd(ss.str());

        if (!rv)
        {
            MXB_SERROR("Could not create user " << au.mariadb_user << ": " << m_db.error());
        }
    }

    return rv;
}

bool UserManagerMariaDB::do_remove_user(const string& db, const string& user)
{
    string mariadb_user = get_mariadb_user(db, nosql::escape_essential_chars(user));

    ostringstream ss;
    ss << "DELETE FROM " << m_table << " WHERE mariadb_user = '" << mariadb_user << "'";

    bool rv = m_db.cmd(ss.str());

    if (!rv)
    {
        MXB_SERROR("Could not remove user " << mariadb_user << ": " << m_db.error());
    }

    return rv;
}

bool UserManagerMariaDB::user_info_from_result(mxb::QueryResult* pResult, UserManager::UserInfo* pInfo) const
{
    // TODO: A bit of unnecessary work is made here if 'pInfo' is null.

    bool rv = true;

    UserManager::UserInfo info;

    info.mariadb_user = pResult->get_string(0);
    info.db = pResult->get_string(1);
    info.user = pResult->get_string(2);
    info.host = pResult->get_string(3);

    string data = decrypt_data(pResult->get_string(4), info.mariadb_user);
    rv = !data.empty();

    if (rv)
    {
        mxb::Json json;

        rv = json.load_string(data);

        if (rv)
        {
            vector<nosql::role::Role> roles;
            if (nosql::role::from_json(json.get_array("roles"), &roles))
            {
                info.roles = std::move(roles);
            }
            else
            {
                MXB_ERROR("The 'roles' value of '%s' is not valid.", info.mariadb_user.c_str());
                rv = false;
            }

            if (rv)
            {
                info.pwd_sha1_b64 = json.get_string("pwd_sha1_b64");
                info.uuid = json.get_string("uuid");
                info.custom_data = json.get_object("custom_data").to_string(mxb::Json::Format::NORMAL);
                info.salt_sha1_b64 = json.get_string("salt_sha1_b64");
                info.salt_sha256_b64 = json.get_string("salt_sha256_b64");
                info.salted_pwd_sha1_b64 = json.get_string("salted_pwd_sha1_b64");
                info.salted_pwd_sha256_b64 = json.get_string("salted_pwd_sha256_b64");

                if (!info.salt_sha1_b64.empty())
                {
                    info.mechanisms.push_back(nosql::scram::Mechanism::SHA_1);
                }

                if (!info.salt_sha256_b64.empty())
                {
                    info.mechanisms.push_back(nosql::scram::Mechanism::SHA_256);
                }

                if (pInfo)
                {
                    *pInfo = std::move(info);
                }
            }
        }
        else
        {
            MXB_SERROR("Could not load Json data of '" << info.mariadb_user << "': " << json.error_msg());
        }
    }

    return rv;
}

vector<UserManager::UserInfo> UserManagerMariaDB::user_infos_from_result(mxb::QueryResult* pResult) const
{
    mxb_assert(pResult->get_col_count() == 5);

    vector<UserManager::UserInfo> rv;

    while (pResult->next_row())
    {
        UserManager::UserInfo ui;
        if (user_info_from_result(pResult, &ui))
        {
            rv.push_back(ui);
        }
    }

    return rv;
}

bool UserManagerMariaDB::do_get_info(const string& mariadb_user, UserInfo* pInfo) const
{
    bool rv = true;

    ostringstream sql;
    sql << "SELECT mariadb_user, db, user, host, data FROM " << m_table
        << " WHERE mariadb_user = '" << mariadb_user << "'";

    auto sResult = m_db.query(sql.str());

    if (sResult)
    {
        if (sResult->get_row_count() == 1)
        {
            mxb_assert(sResult->get_col_count() == 5);
            sResult->next_row();

            rv = user_info_from_result(sResult.get(), pInfo);
        }
        else
        {
            MXB_SINFO("User '" << mariadb_user << "' not found.");
            rv = false;
        }
    }
    else
    {
        MXB_SERROR("Could not fetch user info for single user: " << m_db.error());
        rv = false;
    }

    return rv;
}

vector<UserManager::UserInfo> UserManagerMariaDB::do_get_infos() const
{
    vector<UserInfo> rv;

    ostringstream sql;
    sql << "SELECT mariadb_user, db, user, host, data FROM " << m_table;

    auto sResult = m_db.query(sql.str());

    if (sResult)
    {
        rv = user_infos_from_result(sResult.get());
    }
    else
    {
        MXB_SERROR("Could not fetch all user infos: " << m_db.error());
    }

    return rv;

}

vector<UserManager::UserInfo> UserManagerMariaDB::do_get_infos(const string& db) const
{
    vector<UserInfo> rv;

    ostringstream sql;
    sql << "SELECT mariadb_user, db, user, host, data FROM " << m_table
        << " WHERE db = '" << db << "'";

    auto sResult = m_db.query(sql.str());

    if (sResult)
    {
        rv = user_infos_from_result(sResult.get());
    }
    else
    {
        MXB_SERROR("Could not fetch user infos or particular db: " << m_db.error());
    }

    return rv;
}

vector<UserManager::UserInfo> UserManagerMariaDB::do_get_infos(const vector<string>& mariadb_users) const
{
    vector<UserInfo> rv;

    if (!mariadb_users.empty())
    {
        ostringstream sql;
        sql << "SELECT mariadb_user, db, user, host, data FROM " << m_table
            << " WHERE ";

        for (auto it = mariadb_users.begin(); it != mariadb_users.end(); ++it)
        {
            if (it != mariadb_users.begin())
            {
                sql << " OR ";
            }

            sql << "mariadb_user = `" << *it << "`";
        }

        auto sResult = m_db.query(sql.str());

        if (sResult)
        {
            rv = user_infos_from_result(sResult.get());
        }
        else
        {
            MXB_SERROR("Could not fetch user infos of particular users: " << m_db.error());
        }
    }

    return rv;
}

vector<UserManager::Account> UserManagerMariaDB::do_get_accounts(const string& db) const
{
    vector<Account> rv;

    ostringstream sql;
    sql << "SELECT mariadb_user, user, db, host FROM " << m_table
        << " WHERE db = '" << db << "'";

    auto sResult = m_db.query(sql.str());

    if (sResult)
    {
        mxb_assert(sResult->get_col_count() == 4);

        while (sResult->next_row())
        {
            Account account;
            account.mariadb_user = sResult->get_string(0);
            account.user = sResult->get_string(1);
            account.db = sResult->get_string(2);
            account.host = sResult->get_string(3);

            rv.push_back(account);
        }
    }
    else
    {
        MXB_SERROR("Could not fetch account data: " << m_db.error());
    }

    return rv;
}

bool UserManagerMariaDB::do_remove_accounts(const vector<Account>& accounts) const
{
    bool rv = true;

    if (!accounts.empty())
    {
        ostringstream sql;
        sql << "DELETE FROM " << m_table << " WHERE ";

        for (auto it = accounts.begin(); it != accounts.end(); ++it)
        {
            const auto& account = *it;

            if (it != accounts.begin())
            {
                sql << " OR ";
            }

            sql << "mariadb_user = '" << account.mariadb_user << "'";
        }

        rv = m_db.cmd(sql.str());

        if (!rv)
        {
            MXB_SERROR("Could not delete accounts: " << m_db.error());
        }
    }

    return rv;
}

bool UserManagerMariaDB::do_update(const string& db,
                                   const string& user,
                                   uint32_t what,
                                   const Update& data) const
{
    mxb_assert((what & Update::MASK) != 0);

    string mariadb_user = get_mariadb_user(db, nosql::escape_essential_chars(user));

    UserInfo info;
    bool rv = do_get_info(mariadb_user, &info);

    if (rv)
    {
        if (what & Update::CUSTOM_DATA)
        {
            info.custom_data = data.custom_data;
        }

        if (what & Update::PWD)
        {
            auto pwd = nosql::escape_essential_chars(data.pwd);
            vector<uint8_t> pwd_sha1 = crypto::sha_1(pwd);

            info.pwd_sha1_b64 = mxs::to_base64(pwd_sha1);

            string salt_sha1_b64;
            string salted_pwd_sha1_b64;
            string salt_sha256_b64;
            string salted_pwd_sha256_b64;

            mxb_assert(!data.mechanisms.empty());
            get_salts_and_salted_passwords(user, pwd,
                                           data.mechanisms,
                                           &info.salt_sha1_b64, &info.salted_pwd_sha1_b64,
                                           &info.salt_sha256_b64, &info.salted_pwd_sha256_b64);
        }

        if ((what & Update::MECHANISMS) && !(what & Update::PWD))
        {
            auto begin = data.mechanisms.begin();
            auto end = data.mechanisms.end();

            if (std::find(begin, end, scram::Mechanism::SHA_1) == end)
            {
                info.salt_sha1_b64.clear();
                info.salted_pwd_sha1_b64.clear();
            }

            if (std::find(begin, end, scram::Mechanism::SHA_256) == end)
            {
                info.salt_sha256_b64.clear();
                info.salted_pwd_sha256_b64.clear();
            }
        }

        if (what & Update::ROLES)
        {
            info.roles = data.roles;
        }

        mxb::Json json;

        json.set_string("salt_sha1_b64", info.salt_sha1_b64);
        json.set_string("salted_pwd_sha1_b64", info.salted_pwd_sha1_b64);
        json.set_string("salt_sha256_b64", info.salt_sha256_b64);
        json.set_string("salted_pwd_sha256_b64", info.salted_pwd_sha256_b64);
        json.set_string("pwd_sha1_b64", info.pwd_sha1_b64);
        json.set_string("uuid", info.uuid);
        if (!info.custom_data.empty())
        {
            mxb::Json custom_data(mxb::Json::Type::OBJECT);
            MXB_AT_DEBUG(bool loaded =) custom_data.load_string(info.custom_data);
            mxb_assert(loaded);

            json.set_object("custom_data", custom_data);
        }
        json.set_object("roles", role::to_json_array(info.roles));

        string strdata = encrypt_data(json, mariadb_user);
        rv = !strdata.empty();

        if (rv)
        {
            ostringstream sql;
            sql << "UPDATE " << m_table << " SET data = '" << strdata << "' "
                << "WHERE mariadb_user = '" << mariadb_user << "'";

            rv = m_db.cmd(sql.str());

            if (!rv)
            {
                MXB_SERROR("Could not update user " << mariadb_user << ": " << m_db.error());
            }
        }
    }

    return rv;
}

}

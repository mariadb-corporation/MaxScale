/*
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

#include "postgresprotocol.hh"
#include "pgusermanager.hh"
#include <maxbase/format.hh>
#include <maxbase/threadpool.hh>
#include <maxscale/config.hh>
#include <maxscale/protocol/postgresql/module_names.hh>
#include <maxscale/secrets.hh>
#include <maxscale/service.hh>

using std::string;
using std::vector;
using Guard = std::lock_guard<std::mutex>;
using ServerType = SERVER::VersionInfo::Type;

namespace
{
constexpr auto acquire = std::memory_order_acquire;
constexpr auto relaxed = std::memory_order_relaxed;
constexpr auto npos = string::npos;
}

PgUserManager::PgUserManager()
    : m_userdb(std::make_shared<PgUserDatabase>())
{
}

std::string PgUserManager::protocol_name() const
{
    return MXS_POSTGRESQL_PROTOCOL_NAME;
}

std::unique_ptr<mxs::UserAccountCache> PgUserManager::create_user_account_cache()
{
    auto cache = std::make_unique<PgUserCache>(*this);
    cache->update_from_master();
    return cache;
}

PgUserManager::UserDBInfo PgUserManager::get_user_database() const
{
    // A lock is needed to ensure both the db and version number are from the same update.
    Guard guard(m_userdb_lock);
    return UserDBInfo{m_userdb, m_userdb_version.load(relaxed)};
}

int PgUserManager::userdb_version() const
{
    return m_userdb_version.load(acquire);
}

json_t* PgUserManager::users_to_json() const
{
    SUserDB ptr_copy;
    {
        Guard guard(m_userdb_lock);
        ptr_copy = m_userdb;
    }
    return ptr_copy->users_to_json();
}

bool PgUserManager::update_users()
{
    auto sett = get_load_settings();

    auto temp_userdata = std::make_unique<PgUserDatabase>();
    bool file_enabled = !sett.users_file_path.empty();

    bool success = false;
    string msg;
    if (file_enabled && sett.users_file_usage == UsersFileUsage::FILE_ONLY_ALWAYS)
    {
        // TODO: load from json file
    }
    else
    {
        std::tie(success, msg) = load_users_from_backends(std::move(sett.conn_user), std::move(sett.conn_pw),
                                                          std::move(sett.backends), *temp_userdata);
        if (file_enabled && sett.users_file_usage == UsersFileUsage::ADD_WHEN_LOAD_OK && success)
        {
            // TODO: load from json file
        }
    }

    if (success)
    {
        string total_msg = mxb::string_printf("Read %s for service '%s'.", msg.c_str(), svc_name());
        if (temp_userdata->equal_contents(*m_userdb))
        {
            MXB_INFO("%s Fetched data was identical to existing user data.", total_msg.c_str());
        }
        else
        {
            // Data changed, update main user db. Cache update message is sent by the caller.
            std::unique_lock<std::mutex> lock(m_userdb_lock);
            m_userdb = std::move(temp_userdata);
            m_userdb_version++;
            lock.unlock();
            MXB_NOTICE("%s", total_msg.c_str());
        }
    }
    return success;
}

std::tuple<bool, std::string>
PgUserManager::load_users_from_backends(string&& conn_user, string&& conn_pw, vector<SERVER*>&& backends,
                                        PgUserDatabase& output)
{
    mxp::PgSQL con;
    auto& sett = con.connection_settings();
    sett.user = std::move(conn_user);
    sett.password = mxs::decrypt_password(conn_pw);

    mxs::Config& glob_config = mxs::Config::get();
    sett.connect_timeout = glob_config.auth_conn_timeout.get().count();
    sett.read_timeout = glob_config.auth_read_timeout.get().count();
    sett.write_timeout = glob_config.auth_write_timeout.get().count();

    const bool union_over_bes = union_over_backends();

    // Filter out unusable backends.
    auto is_unusable = [](const SERVER* srv) {
        return !srv->active() || !srv->is_usable();
    };
    auto erase_iter = std::remove_if(backends.begin(), backends.end(), is_unusable);
    backends.erase(erase_iter, backends.end());
    if (backends.empty() && m_warn_no_servers.load(relaxed))
    {
        MXB_ERROR("No valid servers from which to query PostgreSQL user accounts found.");
    }

    // Order backends so that the master is checked first.
    auto compare = [](const SERVER* lhs, const SERVER* rhs) {
        return (lhs->is_master() && !rhs->is_master())
               || (lhs->is_slave() && (!rhs->is_master() && !rhs->is_slave()));
    };
    std::sort(backends.begin(), backends.end(), compare);

    bool got_data = false;
    std::vector<string> source_servernames;
    const char users_query_failed[] = "Failed to query server '%s' for user account info. %s";

    for (auto srv : backends)
    {
        // Different backends may have different ssl settings so need to update.
        sett.ssl = srv->ssl_config();

        if (con.open(srv->address(), srv->port(), "postgres"))
        {
            auto load_result = LoadResult::QUERY_FAILED;

            // If server version is unknown (no monitor), update its version info.
            auto& srv_info = srv->info();
            if (srv_info.type() == ServerType::UNKNOWN)
            {
                auto new_info = con.get_version_info();
                if (new_info.version != 0)
                {
                    srv->set_version(SERVER::BaseType::POSTGRESQL, new_info.version, new_info.info, 0);
                }
            }

            switch (srv_info.type())
            {
            case ServerType::POSTGRESQL:
                load_result = load_users_pg(con, output);
                break;

            default:
                // Cannot query these types.
                MXB_ERROR("Cannot fetch user accounts for service %s from server %s. Server type is %s "
                          "when a PostgreSQL server was expected.",
                          svc_name(), srv->name(), srv_info.type_string().c_str());
                break;
            }

            switch (load_result)
            {
            case LoadResult::SUCCESS:
                // Print successes after iteration is complete.
                source_servernames.emplace_back(srv->name());
                got_data = true;
                break;

            case LoadResult::QUERY_FAILED:
                MXB_ERROR(users_query_failed, srv->name(), con.error());
                break;

            case LoadResult::INVALID_DATA:
                MXB_ERROR("Received invalid data from '%s' when querying user accounts.", srv->name());
                break;
            }

            if (got_data && !union_over_bes)
            {
                break;
            }
        }
        else
        {
            MXB_ERROR(users_query_failed, srv->name(), con.error());
        }
    }

    string msg;
    if (got_data)
    {
        string datasource = mxb::create_list_string(source_servernames, ", ", " and ", "'");
        msg = mxb::string_printf("%d host and %d role entries from %s",
                                 output.n_hba_entries(), output.n_auth_entries(), datasource.c_str());
    }
    return {got_data, msg};
}

PgUserManager::LoadResult PgUserManager::load_users_pg(mxp::PgSQL& con, PgUserDatabase& output)
{
    auto read_list = [](std::string_view list_str) {
        std::vector<string> rval;
        // The list should be enclosed in {}.
        if (list_str.length() >= 2 && list_str.front() == '{' && list_str.back() == '}')
        {
            std::string_view eff_list_str = {list_str.data() + 1, list_str.length() - 2};
            rval = mxb::strtok(eff_list_str, ", ");
        }
        return rval;
    };

    // TODO: use multiquery
    // TODO: handle 'union_over_backends' somehow.
    auto load_res = LoadResult::QUERY_FAILED;
    auto hba_result = con.query("select line_number, type, database, user_name, address, netmask, "
                                "auth_method from pg_hba_file_rules;");
    if (hba_result)
    {
        if (hba_result->get_col_count() == 7)
        {
            int64_t ind_lineno = 0;
            int64_t ind_type = 1;
            int64_t ind_dbs = 2;
            int64_t ind_unames = 3;
            int64_t ind_addr = 4;
            int64_t ind_netmask = 5;
            int64_t ind_auth_method = 6;

            while (hba_result->next_row())
            {
                // Skip domain socket and gssapi encryption for now. Interpret the other connection types as
                // normal tcp users.
                auto conn_type = hba_result->get_string(ind_type);
                if (conn_type == "host" || conn_type == "hostssl" || conn_type == "hostnossl"
                    || conn_type == "hostnogssenc")
                {
                    PgUserDatabase::HbaEntry new_entry;
                    new_entry.lineno = hba_result->get_int(ind_lineno);
                    new_entry.usernames = read_list(hba_result->get_string(ind_unames));
                    new_entry.address = hba_result->get_string(ind_addr);
                    new_entry.mask = hba_result->get_string(ind_netmask);
                    new_entry.db_names = read_list(hba_result->get_string(ind_dbs));
                    new_entry.auth_method = hba_result->get_string(ind_auth_method);
                    output.add_hba_entry(std::move(new_entry));
                }
            }
            load_res = LoadResult::SUCCESS;
        }
        else
        {
            load_res = LoadResult::INVALID_DATA;
        }
    }

    if (load_res == LoadResult::SUCCESS)
    {
        auto authid_result = con.query("select rolname, rolpassword, rolsuper, rolinherit, rolcanlogin "
                                       "from pg_authid;");
        if (authid_result)
        {
            if (authid_result->get_col_count() == 5)
            {
                int64_t ind_name = 0;
                int64_t ind_pw = 1;
                int64_t ind_super = 2;
                int64_t ind_inherit = 3;
                int64_t ind_login = 4;

                auto read_bool = [&authid_result](int64_t col_ind) {
                    auto str = authid_result->get_string(col_ind);
                    return str == "t";
                };

                while (authid_result->next_row())
                {
                    AuthIdEntry new_entry;
                    new_entry.name = authid_result->get_string(ind_name);
                    new_entry.password = authid_result->get_string(ind_pw);
                    new_entry.super = read_bool(ind_super);
                    new_entry.inherit = read_bool(ind_inherit);
                    new_entry.can_login = read_bool(ind_login);
                    output.add_authid_entry(std::move(new_entry));
                }
                load_res = LoadResult::SUCCESS;
            }
            else
            {
                load_res = LoadResult::INVALID_DATA;
            }
        }
        else
        {
            load_res = LoadResult::QUERY_FAILED;
        }
    }

    return load_res;
}

json_t* PgUserDatabase::users_to_json() const
{
    auto rval = json_array();
    return rval;
}

bool PgUserDatabase::equal_contents(const PgUserDatabase& rhs) const
{
    return m_hba_entries == rhs.m_hba_entries && m_auth_entries == rhs.m_auth_entries;
}

int PgUserDatabase::n_hba_entries() const
{
    return m_hba_entries.size();
}

void PgUserDatabase::add_hba_entry(PgUserDatabase::HbaEntry&& entry)
{
    m_hba_entries.emplace_back(std::move(entry));
}

void PgUserDatabase::add_authid_entry(AuthIdEntry&& entry)
{
    // Names should be unique. Copies are possible when summing over all backends.
    string key = entry.name;
    m_auth_entries.emplace(std::move(key), std::move(entry));
}

int PgUserDatabase::n_auth_entries() const
{
    return m_auth_entries.size();
}

const PgUserDatabase::HbaEntry*
PgUserDatabase::find_hba_entry(std::string_view username, std::string_view host, std::string_view db) const
{
    return find_hba_entry(username, host, db, HostPatternMode::MATCH);
}

const PgUserDatabase::HbaEntry*
PgUserDatabase::find_hba_entry(std::string_view username, std::string_view db) const
{
    return find_hba_entry(username, "", db, HostPatternMode::SKIP);
}

const PgUserDatabase::HbaEntry*
PgUserDatabase::find_hba_entry(std::string_view username, std::string_view host, std::string_view db,
                               PgUserDatabase::HostPatternMode mode) const
{
    const PgUserDatabase::HbaEntry* rval = nullptr;
    for (const auto& entry : m_hba_entries)
    {
        // Postgres goes through the entries in order, locking on to the line which matches client address,
        // requested database, and user name.

        // TODO: add proper address matching and mask support.
        bool addr_match = (mode == HostPatternMode::SKIP || entry.address == "all"
            || entry.address == host);

        if (addr_match)
        {
            bool username_match = false;
            for (const auto& allowed_user : entry.usernames)
            {
                if (allowed_user == "all" || allowed_user == username)
                {
                    username_match = true;
                    break;
                }
                else if (allowed_user[0] == '+')
                {
                    // TODO: search group membership.
                }
            }

            if (username_match)
            {
                bool db_match = false;
                for (const auto& allowed_db : entry.db_names)
                {
                    if (allowed_db == "all" || allowed_db == db
                        || (allowed_db == "sameuser" && db == username))
                    {
                        db_match = true;
                        break;
                    }
                    else if (allowed_db == "samerole" || allowed_db == "samegroup")
                    {
                        // TODO:: check if group db exists and user is member.
                    }
                }

                if (db_match)
                {
                    // If client is marked for rejection, return null.
                    if (entry.auth_method != "reject")
                    {
                        rval = &entry;
                    }
                    break;
                }
            }
        }
    }
    return rval;
}

const AuthIdEntry* PgUserDatabase::find_authid_entry(const std::string& username) const
{
    const AuthIdEntry* rval = nullptr;
    if (auto it = m_auth_entries.find(username); it != m_auth_entries.end())
    {
        rval = &it->second;
    }
    return rval;
}

PgUserCache::PgUserCache(const PgUserManager& master)
    : m_master(master)
{
}

void PgUserCache::update_from_master()
{
    if (m_userdb_version < m_master.userdb_version())
    {
        // Master db has updated data, copy the shared pointer.
        auto db_info = m_master.get_user_database();
        m_userdb = std::move(db_info.user_db);
        m_userdb_version = db_info.version;
    }
}

bool PgUserCache::can_update_immediately() const
{
    // Same as with MariaDB.
    return m_userdb_version < m_master.userdb_version() || m_master.can_update_immediately();
}

int PgUserCache::version() const
{
    return m_userdb_version;
}

UserEntryResult
PgUserCache::find_user(const std::string& user, const std::string& host, const std::string& db,
                       MatchHost match_host) const
{
    using HbaEntry = PgUserDatabase::HbaEntry;
    using AuthIdEntry = AuthIdEntry;

    // PG tells the client if a matching hba entry was not found, regardless of password.
    UserEntryResult res;

    // Try to find an entry. If host pattern matching is disabled, match only username.
    const HbaEntry* hba_entry = (match_host == MatchHost::YES) ? m_userdb->find_hba_entry(user, host, db) :
        m_userdb->find_hba_entry(user, db);
    if (hba_entry)
    {
        res.line_no = hba_entry->lineno;
        res.auth_method = hba_entry->auth_method;

        const AuthIdEntry* auth_id = m_userdb->find_authid_entry(user);
        if (auth_id)
        {
            res.type = UserEntryType::USER_ACCOUNT_OK;
            res.authid_entry = *auth_id;
        }
        else
        {
            // TODO: the error message seems to depend on the auth_method. Tune later.
            res.type = UserEntryType::NO_AUTH_ID_ENTRY;
        }
    }
    else
    {
        res.type = UserEntryType::NO_HBA_ENTRY;
    }

    return res;
}

bool PgUserDatabase::HbaEntry::operator==(const PgUserDatabase::HbaEntry& rhs) const
{
    return lineno == rhs.lineno && usernames == rhs.usernames && db_names == rhs.db_names
           && address == rhs.address && mask == rhs.mask && auth_method == rhs.auth_method;
}

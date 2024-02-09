/*
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

#include <maxscale/ccdefs.hh>

#include <condition_variable>
#include <map>
#include <set>
#include <thread>
#include <maxbase/queryresult.hh>
#include <maxpgsql/pg_connector.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/server.hh>
#include <maxscale/base_user_manager.hh>
#include "pgprotocoldata.hh"
#include "pgauthenticatormodule.hh"

/**
 * This class contains user data retrieved from the PostgreSQL-database.
 */
class PgUserDatabase
{
public:
    json_t* users_to_json() const;
    bool    equal_contents(const PgUserDatabase& rhs) const;
    int     n_hba_entries() const;
    int     n_auth_entries() const;

    struct HbaEntry
    {
        int                      lineno {0};
        std::vector<std::string> usernames;
        std::vector<std::string> db_names;
        std::string              address;
        std::string              mask;
        std::string              auth_method;

        bool operator==(const HbaEntry& rhs) const;
    };

    void add_hba_entry(HbaEntry&& entry);
    void add_authid_entry(AuthIdEntry&& entry);

    const HbaEntry*
    find_hba_entry(std::string_view username, std::string_view host, std::string_view db) const;

    const HbaEntry* find_hba_entry(std::string_view username, std::string_view db) const;

    const AuthIdEntry* find_authid_entry(const std::string& username) const;

private:
    enum class HostPatternMode
    {
        SKIP,
        MATCH,
    };
    const HbaEntry* find_hba_entry(std::string_view username, std::string_view host, std::string_view db,
                                   HostPatternMode mode) const;

    std::vector<HbaEntry>              m_hba_entries;   // Must be kept in server order
    std::map<std::string, AuthIdEntry> m_auth_entries;
};

class PgUserManager : public mxs::BaseUserManager
{
public:
    using SUserDB = std::shared_ptr<const PgUserDatabase>;
    PgUserManager();
    ~PgUserManager() override = default;

    std::unique_ptr<mxs::UserAccountCache> create_user_account_cache() override;

    std::string protocol_name() const override;
    int         userdb_version() const;

    struct UserDBInfo
    {
        SUserDB user_db;
        int     version {0};
    };

    /**
     * Get database info.
     *
     * @return Database pointer and version
     */
    UserDBInfo get_user_database() const;

    json_t* users_to_json() const override;

private:
    mutable std::mutex m_userdb_lock;       /**< Protects UserDatabase from concurrent access */
    SUserDB            m_userdb;            /**< Contains user account info */
    std::atomic_int    m_userdb_version {0};/**< How many times the user database has changed */

    bool update_users() override;

    enum class LoadResult
    {
        SUCCESS,
        QUERY_FAILED,
        INVALID_DATA,
    };

    std::tuple<bool, std::string>
    load_users_from_backends(std::string&& conn_user, std::string&& conn_pw,
                             std::vector<SERVER*>&& backends, PgUserDatabase& output);

    PgUserManager::LoadResult load_users_pg(mxp::PgSQL& con, PgUserDatabase& output);
};

class PgUserCache : public mxs::UserAccountCache
{
public:
    PgUserCache(const PgUserManager& master);
    ~PgUserCache() override = default;

    void update_from_master() override;
    bool can_update_immediately() const;
    int  version() const;

    enum class MatchHost {YES, NO};
    UserEntryResult
    find_user(const std::string& user, const std::string& host, const std::string& db,
              MatchHost match_host) const;

private:
    const PgUserManager&   m_master;    /**< User database master copy */
    PgUserManager::SUserDB m_userdb;    /**< Local pointer to user database */

    /** Version of local copy. Starts at -1 so that it can update from master which starts at 0. */
    int m_userdb_version {-1};
};

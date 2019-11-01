/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/protocol/mariadb/module_names.hh>
#define MXS_MODULE_NAME MXS_MARIADBAUTH_AUTHENTICATOR_NAME

#include <maxscale/ccdefs.hh>

#include <stdint.h>
#include <arpa/inet.h>
#include <mysql.h>
#include <maxscale/dcb.hh>
#include <maxscale/buffer.hh>
#include <maxscale/service.hh>
#include <maxscale/sqlite3.h>
#include <maxscale/protocol/mariadb/authenticator.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/workerlocal.hh>

/** Cache directory and file names */
static const char DBUSERS_DIR[] = "cache";
static const char DBUSERS_FILE[] = "dbusers.db";

/** The table name where we store the users */
#define MYSQLAUTH_USERS_TABLE_NAME "mysqlauth_users"

/** The table name where we store the users */
#define MYSQLAUTH_DATABASES_TABLE_NAME "mysqlauth_databases"

/** CREATE TABLE statement for the in-memory users table */
static const char users_create_sql[] =
    "CREATE TABLE IF NOT EXISTS " MYSQLAUTH_USERS_TABLE_NAME
    "(user varchar(255), host varchar(255), db varchar(255), anydb boolean, password text)";

/** CREATE TABLE statement for the in-memory databases table */
static const char databases_create_sql[] =
    "CREATE TABLE IF NOT EXISTS " MYSQLAUTH_DATABASES_TABLE_NAME "(db varchar(255))";

/** PRAGMA configuration options for SQLite */
static const char pragma_sql[] = "PRAGMA JOURNAL_MODE=NONE";

/** Query that checks if there's a grant for the user being authenticated */
static const char mysqlauth_validate_user_query[] =
    "SELECT password FROM " MYSQLAUTH_USERS_TABLE_NAME
    " WHERE user = '%s' AND ( '%s' = host OR '%s' LIKE host)"
    " AND (anydb = '1' OR '%s' IN ('', 'information_schema') OR '%s' LIKE db)"
    " LIMIT 1";

/** Query that checks if there's a grant for the user being authenticated */
static const char mysqlauth_validate_user_query_lower[] =
    "SELECT password FROM " MYSQLAUTH_USERS_TABLE_NAME
    " WHERE user = '%s' AND ( '%s' = host OR '%s' LIKE host)"
    " AND (anydb = '1' OR LOWER('%s') IN ('', 'information_schema') OR LOWER('%s') LIKE LOWER(db))"
    " LIMIT 1";

/** Query that only checks if there's a matching user */
static const char mysqlauth_skip_auth_query[] =
    "SELECT password FROM " MYSQLAUTH_USERS_TABLE_NAME
    " WHERE user = '%s' AND (anydb = '1' OR '%s' IN ('', 'information_schema') OR '%s' LIKE db)"
    " LIMIT 1";

/** Query that checks that the database exists */
static const char mysqlauth_validate_database_query[] =
    "SELECT * FROM " MYSQLAUTH_DATABASES_TABLE_NAME " WHERE db = '%s' LIMIT 1";
static const char mysqlauth_validate_database_query_lower[] =
    "SELECT * FROM " MYSQLAUTH_DATABASES_TABLE_NAME " WHERE LOWER(db) = LOWER('%s') LIMIT 1";

/** Delete query used to clean up the database before loading new users */
static const char delete_users_query[] = "DELETE FROM " MYSQLAUTH_USERS_TABLE_NAME;

/** Delete query used to clean up the database before loading new users */
static const char delete_databases_query[] = "DELETE FROM " MYSQLAUTH_DATABASES_TABLE_NAME;

/** The insert query template which adds users to the mysqlauth_users table */
static const char insert_user_query[] =
    "INSERT OR REPLACE INTO " MYSQLAUTH_USERS_TABLE_NAME " VALUES ('%s', '%s', %s, %s, %s)";

/** The insert query template which adds the databases to the table */
static const char insert_database_query[] =
    "INSERT OR REPLACE INTO " MYSQLAUTH_DATABASES_TABLE_NAME " VALUES ('%s')";

static const char dump_users_query[] =
    "SELECT user, host, db, anydb, password FROM " MYSQLAUTH_USERS_TABLE_NAME;

static const char dump_databases_query[] =
    "SELECT db FROM " MYSQLAUTH_DATABASES_TABLE_NAME;

/** Used for NULL value creation in the INSERT query */
static const char null_token[] = "NULL";

/** Flags for sqlite3_open_v2() */
static int db_flags = SQLITE_OPEN_READWRITE
    | SQLITE_OPEN_CREATE
    | SQLITE_OPEN_NOMUTEX;

class MariaDBAuthenticatorModule : public mariadb::AuthenticatorModule
{
public:
    static MariaDBAuthenticatorModule* create(char** options);
    ~MariaDBAuthenticatorModule() override = default;

    mariadb::SClientAuth  create_client_authenticator() override;
    mariadb::SBackendAuth create_backend_authenticator() override;

    int         load_users(SERVICE* service) override;
    void        diagnostics(DCB* output) override;
    json_t*     diagnostics_json() override;
    uint64_t    capabilities() const override;
    std::string supported_protocol() const override;
    std::string name() const override;

    /**
     * @brief Get the thread-specific SQLite handle
     *
     * @return The thread-specific handle
     */
    sqlite3* get_handle();

    mxs::WorkerLocal<sqlite3*> m_handle;    /**< SQLite3 database handle */

    char* m_cache_dir {nullptr};            /**< Custom cache directory location */
    bool  m_inject_service_user {true};     /**< Inject the service user into the list of users */
    bool  m_skip_auth {false};              /**< Authentication will always be successful */
    bool  m_check_permissions {true};
    bool  m_lower_case_table_names {false};     /**< Disable database case-sensitivity */

private:
    int  get_users(SERVICE* service, bool skip_local, SERVER** srv);
    int  get_users_from_server(MYSQL* con, SERVER* server, SERVICE* service);
    bool add_service_user(SERVICE* service);
};

class MariaDBClientAuthenticator : public mariadb::ClientAuthenticatorT<MariaDBAuthenticatorModule>
{
public:
    MariaDBClientAuthenticator(MariaDBAuthenticatorModule* module);
    ~MariaDBClientAuthenticator() override = default;

    bool extract(GWBUF* buffer, MYSQL_session* session) override;
    int  authenticate(DCB* client) override;

    int reauthenticate(DCB* generic_dcb, uint8_t* scramble, size_t scramble_len, const ByteVec& auth_token,
                       uint8_t* output_token) override;

private:
    /**
     * @brief Verify the user has access to the database
     *
     * @param dcb          Client DCB
     * @param session      Shared MySQL session
     * @param scramble     The scramble sent to the client in the initial handshake
     * @param scramble_len Length of @c scramble
     * @param auth_token   Authentication token from client
     * @param phase2_scramble_out Output for backend token
     *
     * @return MXS_AUTH_SUCCEEDED if the user has access to the database
     */
    int validate_mysql_user(DCB* dcb, const MYSQL_session* session,
                            const uint8_t* scramble, size_t scramble_len,
                            const mariadb::ClientAuthenticator::ByteVec& auth_token,
                            uint8_t* phase2_scramble_out);
    bool check_database(sqlite3* handle, const char* database);
    bool set_client_data(MYSQL_session* client_data, GWBUF* buffer);

    bool m_correct_authenticator {false};   /*< Is session using mysql_native_password? */
    bool m_auth_switch_sent {false};        /*< Expecting a response to AuthSwitchRequest? */
};

/** Structure representing the authentication state */
class MariaDBBackendSession : public mariadb::BackendAuthenticator
{
public:
    ~MariaDBBackendSession() = default;

    bool extract(DCB* backend, GWBUF* buffer) override;
    bool ssl_capable(DCB* backend) override;
    int  authenticate(DCB* backend) override;

private:
    /** Authentication states */
    enum class State
    {
        NEED_OK,                /**< Waiting for server's OK packet */
        AUTH_OK,                /**< Authentication completed successfully */
        AUTH_FAILED             /**< Authentication failed */
    };

    State state {State::NEED_OK};   /**< Authentication state */
};

/**
 * @brief Add new MySQL user to the internal user database
 *
 * @param handle Database handle
 * @param user   Username
 * @param host   Host
 * @param db     Database
 * @param anydb  Global access to databases
 */
void add_mysql_user(sqlite3* handle,
                    const char* user,
                    const char* host,
                    const char* db,
                    bool anydb,
                    const char* pw);

/**
 * @brief Check if the service user has all required permissions to operate properly.
 *
 * This checks for SELECT permissions on mysql.user, mysql.db and mysql.tables_priv
 * tables and for SHOW DATABASES permissions. If permissions are not adequate,
 * an error message is logged and the service is not started.
 *
 * @param service Service to inspect
 *
 * @return True if service permissions are correct on at least one server, false
 * if permissions are missing or if an error occurred.
 */
bool check_service_permissions(SERVICE* service);

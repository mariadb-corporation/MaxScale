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
#include "pam_auth.hh"

#include <string>
#include <maxsql/mariadb_connector.hh>
#include <maxscale/service.hh>
#include <maxscale/sqlite3.h>
#include "pam_client_session.hh"

/** The instance class for the client side PAM authenticator, created in pam_auth_init() */
class PamAuthenticatorModule : public mariadb::AuthenticatorModule
{
public:
    PamAuthenticatorModule(const PamAuthenticatorModule& orig) = delete;
    PamAuthenticatorModule& operator=(const PamAuthenticatorModule&) = delete;

    static PamAuthenticatorModule* create(char** options);

    int         load_users(SERVICE* service) override;
    json_t*     diagnostics() override;
    uint64_t    capabilities() const override;
    std::string supported_protocol() const override;
    std::string name() const override;

    mariadb::SClientAuth create_client_authenticator() override;
    mariadb::SBackendAuth create_backend_authenticator() override;

    const std::string m_dbname;     /**< Name of the in-memory database */

private:
    using QResult = std::unique_ptr<mxq::QueryResult>;

    PamAuthenticatorModule(const string& dbname);
    bool prepare_tables();

    void add_pam_user(const char* user, const char* host, const char* db, bool anydb,
                      const char* pam_service, bool proxy);
    void delete_old_users();
    bool fetch_anon_proxy_users(SERVER* server, MYSQL* conn);
    void fill_user_arrays(QResult user_res, QResult db_res, QResult roles_mapping_res);

    mxq::SQLite m_sqlite;      /**< SQLite3 database handle */
};

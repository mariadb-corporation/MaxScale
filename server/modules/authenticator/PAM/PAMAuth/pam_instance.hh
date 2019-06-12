/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once
#include "pam_auth.hh"

#include <string>
#include <maxscale/service.hh>
#include <maxscale/sqlite3.h>

/** The instance class for the client side PAM authenticator, created in pam_auth_init() */
class PamInstance
{
public:
    PamInstance(const PamInstance& orig) = delete;
    PamInstance& operator=(const PamInstance&) = delete;

    static PamInstance* create(char** options);

    int     load_users(SERVICE* service);
    void    diagnostic(DCB* dcb);
    json_t* diagnostic_json();

    const std::string m_dbname;     /**< Name of the in-memory database */
    const std::string m_tablename;  /**< The table where users are stored */

private:
    PamInstance(SQLite::SSQLite dbhandle, const std::string& dbname);
    bool prepare_tables();

    void add_pam_user(const char* user, const char* host, const char* db, bool anydb,
                      const char* pam_service, bool proxy);
    void delete_old_users();
    bool fetch_anon_proxy_users(SERVER* server, MYSQL* conn);

    SQLite::SSQLite const m_sqlite;      /**< SQLite3 database handle */
};

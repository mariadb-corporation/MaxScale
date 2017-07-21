#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "pam_auth.hh"

#include <string>
#include <maxscale/service.h>
#include <maxscale/sqlite3.h>

/** The instance class for the client side PAM authenticator, created in pam_auth_init() */
class PamInstance
{
    PamInstance(const PamInstance& orig);
    PamInstance& operator=(const PamInstance&);
public:
    static PamInstance* create(char **options);
    ~PamInstance();
    int load_users(SERVICE* service);

    const std::string m_dbname; /**< Name of the in-memory database */
    const std::string m_tablename; /**< The table where users are stored */
private:
    PamInstance(sqlite3* dbhandle, const std::string& m_dbname, const std::string& tablename);
    void add_pam_user(const char *user, const char *host, const char *db, bool anydb,
                      const char *pam_service);
    void delete_old_users();

    sqlite3 * const m_dbhandle; /**< SQLite3 database handle */
};



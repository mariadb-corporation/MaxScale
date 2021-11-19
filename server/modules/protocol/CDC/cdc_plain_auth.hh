/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#include <maxscale/users.hh>
#include <maxscale/protocol/cdc/cdc.hh>
#include <maxscale/modulecmd.hh>

class CDCClientAuthenticator;

class CDCAuthenticatorModule
{
public:
    static CDCAuthenticatorModule* create(char** options)
    {
        return new(std::nothrow) CDCAuthenticatorModule();
    }

    ~CDCAuthenticatorModule() = default;

    bool load_users(SERVICE* service);

    json_t* diagnostics()
    {
        return m_userdata.diagnostics();
    }

    /**
     * Check user & pw.
     *
     * @param username  String containing username
     * @param auth_data  The encrypted password for authentication
     * @return Authentication status
     * @note Authentication status codes are defined in cdc.h
     */
    int cdc_auth_check(char* username, uint8_t* auth_data);

private:
    int set_service_user(SERVICE* service);
    mxs::Users read_users(char* usersfile);

    mxs::Users m_userdata; // lock-protected user-info
};

bool cdc_add_new_user(const MODULECMD_ARG* args, json_t** output);

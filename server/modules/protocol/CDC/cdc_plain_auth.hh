/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-01
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

    int load_users(SERVICE* service);

    json_t* diagnostics_json()
    {
        return m_userdata.diagnostic_json();
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

class CDCClientAuthenticator
{
public:
    CDCClientAuthenticator(CDCAuthenticatorModule& module)
        : m_module(module)
    {
    }

    ~CDCClientAuthenticator() = default;
    bool extract(DCB* client, GWBUF* buffer);

    bool ssl_capable(DCB* client)
    {
        return false;
    }

    int authenticate(DCB* client);

private:
    bool set_client_data(uint8_t* client_auth_packet, int client_auth_packet_size);

    char    m_user[CDC_USER_MAXLEN + 1] {'\0'}; /*< username for authentication */
    uint8_t m_auth_data[SHA_DIGEST_LENGTH] {0}; /*< Password Hash               */

    CDCAuthenticatorModule& m_module;
};

bool cdc_add_new_user(const MODULECMD_ARG* args, json_t** output);

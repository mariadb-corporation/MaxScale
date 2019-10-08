/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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
#include "pam_auth.hh"

#include <string>
#include <maxscale/authenticator.hh>
#include <maxscale/users.hh>
#include <maxbase/format.hh>

#include "pam_instance.hh"
#include "pam_client_session.hh"
#include "pam_auth_common.hh"

using std::string;

/** Table and column names. The names mostly match the ones in the server. */
const string TABLE_USER = "user";
const string TABLE_DB = "db";
const string TABLE_ROLES_MAPPING = "roles_mapping";

const string FIELD_USER = "user";
const string FIELD_HOST = "host";
const string FIELD_AUTHSTR = "authentication_string";
const string FIELD_DEF_ROLE = "default_role";
const string FIELD_ANYDB = "anydb";
const string FIELD_IS_ROLE = "is_role";
const string FIELD_HAS_PROXY = "proxy_grant";

const string FIELD_DB = "db";
const string FIELD_ROLE = "role";

const int NUM_FIELDS = 6;

extern "C"
{
/**
 * Module handle entry point
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_GA,
        MXS_AUTHENTICATOR_VERSION,
        "PAM authenticator",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::AuthenticatorApiGenerator<PamAuthenticatorModule>::s_api,
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}

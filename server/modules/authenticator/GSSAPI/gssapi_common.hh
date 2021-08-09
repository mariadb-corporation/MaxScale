#pragma once
/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#define MXS_MODULE_NAME "GSSAPIAuth"

#include <maxscale/ccdefs.hh>
#include <gssapi.h>
#include <maxscale/protocol/mariadb/authenticator.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

/** Report GSSAPI errors */
void report_error(OM_uint32 major, OM_uint32 minor);

class GSSAPIAuthenticatorModule : public mariadb::AuthenticatorModule
{
public:
    static GSSAPIAuthenticatorModule* create(mxs::ConfigParameters* options);
    ~GSSAPIAuthenticatorModule() override = default;

    mariadb::SClientAuth  create_client_authenticator() override;
    mariadb::SBackendAuth create_backend_authenticator(mariadb::BackendAuthData& auth_data) override;

    uint64_t    capabilities() const override;
    std::string supported_protocol() const override;
    std::string name() const override;

    const std::unordered_set<std::string>& supported_plugins() const override;

private:
    std::string m_service_principal;    /**< Service principal name given to the client */
};

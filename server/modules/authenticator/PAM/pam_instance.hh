/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "pam_auth_common.hh"
#include <string>
#include <maxbase/pam_utils.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>

class SERVICE;

/** The instance class for the client side PAM authenticator, created in pam_auth_init() */
class PamAuthenticatorModule : public mariadb::AuthenticatorModule
{
public:
    PamAuthenticatorModule(const PamAuthenticatorModule& orig) = delete;
    PamAuthenticatorModule& operator=(const PamAuthenticatorModule&) = delete;

    static PamAuthenticatorModule* create(mxs::ConfigParameters* options);

    uint64_t    capabilities() const override;
    std::string supported_protocol() const override;
    std::string name() const override;

    const std::unordered_set<std::string>& supported_plugins() const override;

    mariadb::SClientAuth  create_client_authenticator() override;
    mariadb::SBackendAuth create_backend_authenticator(mariadb::BackendAuthData& auth_data) override;

private:
    using AuthMode = mxb::pam::AuthMode;

    PamAuthenticatorModule(AuthSettings& settings, PasswordMap&& backend_pwds);

    const AuthSettings m_settings;
    const PasswordMap  m_backend_pwds;
};

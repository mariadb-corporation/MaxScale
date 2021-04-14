/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "mongodbclient.hh"
#include <maxscale/protocol2.hh>
#include "config.hh"


class ProtocolModule : public mxs::ProtocolModule
{
public:
    using AuthenticatorList = std::vector<mxs::SAuthenticatorModule>;

    static ProtocolModule* create(const mxs::ConfigParameters& params);

    std::unique_ptr<mxs::ClientConnection>
    create_client_protocol(MXS_SESSION* session, mxs::Component* component) override;

    std::unique_ptr<mxs::BackendConnection>
    create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component) override;

    std::string auth_default() const override;
    GWBUF*      reject(const std::string& host) override;

    uint64_t    capabilities() const override;
    std::string name() const override;

    std::unique_ptr<mxs::UserAccountManager> create_user_data_manager() override;

    AuthenticatorList create_authenticators(const mxs::ConfigParameters& params) override;

private:
    ProtocolModule(GlobalConfig&& config);

private:
    GlobalConfig m_config;
};

/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

class MySQLProtocolModule : public mxs::ProtocolModule
{
public:
    ~MySQLProtocolModule() override = default;

    static MySQLProtocolModule* create(const mxs::ConfigParameters& params);

    std::unique_ptr<mxs::ClientConnection>
    create_client_protocol(MXS_SESSION* session, mxs::Component* component) override;

    std::unique_ptr<mxs::BackendConnection>
    create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component) override;

    std::string auth_default() const override;
    GWBUF*      reject(const std::string& host) override;

    uint64_t    capabilities() const override;
    std::string name() const override;

    std::unique_ptr<mxs::UserAccountManager> create_user_data_manager() override;

    void user_account_manager_created(mxs::UserAccountManager& manager) override;

    AuthenticatorList create_authenticators(const mxs::ConfigParameters& params) override;

private:
    bool parse_auth_options(const std::string& opts, mxs::ConfigParameters* params_out);
    bool read_authentication_options(mxs::ConfigParameters* params);
    bool read_custom_user_options(mxs::ConfigParameters& params);

    /** Partial user search settings. These settings originate from the listener and do not
     * change once set. */
    mariadb::UserSearchSettings::Listener m_user_search_settings;

    std::unique_ptr<mariadb::UserEntry> m_custom_entry;
};

/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
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

class ProtocolConfig final : public mxs::config::Configuration
{
public:
    ProtocolConfig(const std::string& name);

    mxs::config::Bool allow_replication;
};

class MySQLProtocolModule final : public mxs::ProtocolModule
{
public:
    ~MySQLProtocolModule() override = default;

    static MySQLProtocolModule* create(const std::string& name, mxs::Listener* listener);

    mxs::config::Configuration& getConfiguration() override;

    std::unique_ptr<mxs::ClientConnection>
    create_client_protocol(MXS_SESSION* session, mxs::Component* component) override;

    std::unique_ptr<mxs::BackendConnection>
    create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component) override;

    std::string auth_default() const override;
    GWBUF       make_error(int errnum, const std::string& sqlstate,
                           const std::string& message) const override;
    std::string_view get_sql(const GWBUF& packet) const override;
    std::string describe(const GWBUF& packet, int body_max_len) const override;

    static std::string get_description(const GWBUF& packet, int body_max_len);
    GWBUF              make_query(std::string_view sql) const override;

    uint64_t    capabilities() const override;
    std::string name() const override;
    std::string protocol_name() const override;

    std::unique_ptr<mxs::UserAccountManager> create_user_data_manager() override;

    AuthenticatorList create_authenticators(const mxs::ConfigParameters& params) override;

private:
    MySQLProtocolModule(const std::string& name);
    bool read_authentication_options(mxs::ConfigParameters* params);

    /** Partial user search settings. These settings originate from the listener and do not
     * change once set. */
    mariadb::UserSearchSettings::Listener m_user_search_settings;

    ProtocolConfig m_config;
};

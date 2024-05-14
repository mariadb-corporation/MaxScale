/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "postgresprotocol.hh"
#include <maxscale/protocol2.hh>
#include "pgconfiguration.hh"

class PgParser;
class SERVICE;

class PgProtocolModule final : public mxs::ProtocolModule
{
public:
    using AuthenticatorList = std::vector<mxs::SAuthenticatorModule>;

    static PgProtocolModule* create(const std::string& name, mxs::Listener* pListener);

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    std::unique_ptr<mxs::ClientConnection>
    create_client_protocol(MXS_SESSION* session, mxs::Component* component) override;

    std::unique_ptr<mxs::BackendConnection>
    create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component) override;

    std::string auth_default() const override;
    GWBUF       make_error(int errnum, const std::string& sqlstate,
                           const std::string& message) const override;
    std::string_view get_sql(const GWBUF& packet) const override;
    std::string      describe(const GWBUF& packet, int body_max_len) const override;
    GWBUF            make_query(std::string_view sql) const override;

    uint64_t    capabilities() const override;
    std::string name() const override;
    std::string protocol_name() const override;

    std::unique_ptr<mxs::UserAccountManager> create_user_data_manager() override;

    AuthenticatorList create_authenticators(const mxs::ConfigParameters& params) override;

    bool post_configure();

private:
    PgProtocolModule(std::string name, SERVICE* pService);
    void read_authentication_options(mxs::ConfigParameters* params);

private:
    PgConfiguration           m_config;
    std::unique_ptr<PgParser> m_sParser;
    bool                      m_check_password {true};      /**< Check client password */
    bool                      m_match_host_pattern {true};  /**< Client address must match hba entry */
};

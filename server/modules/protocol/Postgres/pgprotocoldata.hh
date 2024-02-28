/*
 * Copyright (c) 2023 MariaDB plc
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

#include "postgresprotocol.hh"
#include <maxscale/session.hh>
#include <maxscale/history.hh>
#include "pgauthenticatormodule.hh"

class PgProtocolData final : public mxs::ProtocolData
{
public:
    PgProtocolData(size_t limit, bool allow_pruning, bool disable_history);
    ~PgProtocolData();

    bool will_respond(const GWBUF& buffer) const override;
    bool can_recover_state() const override;
    bool is_trx_starting() const override;
    bool is_trx_active() const override;
    bool is_trx_read_only() const override;
    bool is_trx_ending() const override;
    bool is_autocommit() const override;
    bool are_multi_statements_allowed() const override;

    size_t amend_memory_statistics(json_t* memory) const override;
    size_t static_size() const override;
    size_t varying_size() const override;

    void set_connect_params(const uint8_t* begin, const uint8_t* end);
    void set_default_database(std::string_view database);
    void set_application_name(std::string_view name);
    void set_client_encoding(std::string_view encoding);
    void set_user_entry(const UserEntryResult& user_entry);

    const std::vector<uint8_t>& connect_params() const
    {
        return m_params;
    }

    void set_in_trx(bool in_trx);

    mxs::History& history()
    {
        return m_history;
    }

    const std::string&  default_db() const;
    const std::string&  application_name() const;
    const std::string&  client_encoding() const;
    AuthenticationData& auth_data();

private:
    std::string          m_database;
    std::string          m_application_name;
    std::string          m_client_encoding;
    std::vector<uint8_t> m_params;
    bool                 m_in_trx {false};
    AuthenticationData   m_auth_data;

    // Session command history. Contains the commands that modify the session state that are not done as a
    // part of the connection creation. Usually this consists mainly of SET statements that prepare the
    // behavior of the database connection.
    mxs::History m_history;
};

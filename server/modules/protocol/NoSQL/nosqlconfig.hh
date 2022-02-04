/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include <maxscale/config2.hh>
#include "configuration.hh"
#include "nosqlbase.hh"
#include "nosqlcrypto.hh"

namespace nosql
{

// The actual config is copied for each session, so that the config can be
// changed directly from the session itself.
class Config final
{
public:
    Config(const Configuration& config)
        : config_user(config.user)
        , config_password(crypto::sha_1(config.password))
        , user(config.user)
        , password(this->config_password)
        , host(config.host)
        , authentication_required(config.authentication_required)
        , authorization_enabled(config.authorization_enabled)
        , id_length(config.id_length)
        , auto_create_databases(config.auto_create_databases)
        , auto_create_tables(config.auto_create_tables)
        , cursor_timeout(config.cursor_timeout)
        , debug(config.debug)
        , log_unknown_command(config.log_unknown_command)
        , on_unknown_command(config.on_unknown_command)
        , ordered_insert_behavior(config.ordered_insert_behavior)
    {
    }

    bool should_log_in() const
    {
        return this->debug & Configuration::DEBUG_IN;
    }

    bool should_log_out() const
    {
        return this->debug & Configuration::DEBUG_OUT;
    }

    bool should_log_back() const
    {
        return this->debug & Configuration::DEBUG_BACK;
    }

    bool should_authenticate() const
    {
        return this->authentication_required;
    }

    bool should_authorize() const
    {
        return this->authorization_enabled;
    }

    void copy_from(const Config& that)
    {
        // There are const members, hence no assignment operator.
        this->auto_create_databases = that.auto_create_databases;
        this->auto_create_tables = that.auto_create_tables;
        this->cursor_timeout = that.cursor_timeout;
        this->debug = that.debug;
        this->log_unknown_command = that.log_unknown_command;
        this->on_unknown_command = that.on_unknown_command;
        this->ordered_insert_behavior = that.ordered_insert_behavior;
    }

    void copy_from(const std::string& command, const bsoncxx::document::view& doc);
    void copy_to(nosql::DocumentBuilder& doc) const;

    // Can only be changed via MaxScale or by nosqlprotocol itself.
    const std::string          config_user;
    const std::vector<uint8_t> config_password;
    std::string                user;
    std::vector<uint8_t>       password;
    const std::string          host;
    const bool                 authentication_required;
    const bool                 authorization_enabled;
    const int64_t              id_length;

    // Can be changed from the NosQL API.
    bool                                 auto_create_databases;
    bool                                 auto_create_tables;
    std::chrono::seconds                 cursor_timeout;
    uint32_t                             debug;
    bool                                 log_unknown_command;
    Configuration::OnUnknownCommand      on_unknown_command;
    Configuration::OrderedInsertBehavior ordered_insert_behavior;
};

}

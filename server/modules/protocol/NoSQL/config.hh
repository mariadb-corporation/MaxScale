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
#include "nosqlbase.hh"

class ProtocolModule;

class GlobalConfig final : public mxs::config::Configuration
{
public:
    GlobalConfig(const std::string& name, ProtocolModule* instance);

    enum OnUnknownCommand
    {
        RETURN_ERROR,
        RETURN_EMPTY,
    };

    enum class OrderedInsertBehavior
    {
        ATOMIC,
        DEFAULT
    };

    enum
    {
        ID_LENGTH_DEFAULT = 35,
        ID_LENGTH_MIN     = 35,
        ID_LENGTH_MAX     = 2048,
    };

    enum Debug
    {
        DEBUG_NONE = 0,
        DEBUG_IN   = 1,
        DEBUG_OUT  = 2,
        DEBUG_BACK = 4
    };

    enum class Authorization
    {
        DISABLED,
        ENABLED,
    };

    enum
    {
        CURSOR_TIMEOUT_DEFAULT = 60     // seconds
    };

    // Can only be changed via MaxScale
    std::string           user;
    std::string           password;
    std::string           host;
    Authorization         authorization;
    int64_t               id_length {ID_LENGTH_DEFAULT};

    // Can be changed from the NosQL API.
    bool                  auto_create_databases   {true};
    bool                  auto_create_tables      {true};
    std::chrono::seconds  cursor_timeout          {std::chrono::seconds(CURSOR_TIMEOUT_DEFAULT)};
    uint32_t              debug                   { 0 };
    bool                  log_unknown_command     {false};
    OnUnknownCommand      on_unknown_command      {RETURN_ERROR};
    OrderedInsertBehavior ordered_insert_behavior {OrderedInsertBehavior::DEFAULT};

    static mxs::config::Specification& specification();

    // Can only be changed via MaxScale
    static mxs::config::ParamString                      s_user;
    static mxs::config::ParamString                      s_password;
    static mxs::config::ParamString                      s_host;
    static mxs::config::ParamEnum<Authorization>         s_authorization;
    static mxs::config::ParamCount                       s_id_length;

    // Can be changed from the NosQL API.
    static mxs::config::ParamBool                        s_auto_create_databases;
    static mxs::config::ParamBool                        s_auto_create_tables;
    static mxs::config::ParamSeconds                     s_cursor_timeout;
    static mxs::config::ParamEnumMask<Debug>             s_debug;
    static mxs::config::ParamBool                        s_log_unknown_command;
    static mxs::config::ParamEnum<OnUnknownCommand>      s_on_unknown_command;
    static mxs::config::ParamEnum<OrderedInsertBehavior> s_ordered_insert_behavior;

    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override final;

private:
    ProtocolModule* m_instance;
};

namespace nosql
{

// The actual config is copied for each session, so that the config can be
// changed directly from the session itself.
class Config final
{
public:
    Config(const GlobalConfig& config)
        : user(config.user)
        , password(config.password)
        , host(config.host)
        , authorization(config.authorization)
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
        return this->debug & GlobalConfig::DEBUG_IN;
    }

    bool should_log_out() const
    {
        return this->debug & GlobalConfig::DEBUG_OUT;
    }

    bool should_log_back() const
    {
        return this->debug & GlobalConfig::DEBUG_BACK;
    }

    bool should_authorize() const
    {
        return this->authorization == GlobalConfig::Authorization::ENABLED;
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

    // Can only be changed via MaxScale
    std::string                       user;
    std::string                       password;
    const std::string                 host;
    const GlobalConfig::Authorization authorization;
    const int64_t                     id_length;

    // Can be changed from the NosQL API.
    bool                                auto_create_databases;
    bool                                auto_create_tables;
    std::chrono::seconds                cursor_timeout;
    uint32_t                            debug;
    bool                                log_unknown_command;
    GlobalConfig::OnUnknownCommand      on_unknown_command;
    GlobalConfig::OrderedInsertBehavior ordered_insert_behavior;
};

}

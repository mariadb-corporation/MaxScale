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

#include "nosqlprotocol.hh"
#include <maxscale/config2.hh>

class GlobalConfig final : public mxs::config::Configuration
{
public:
    GlobalConfig();
    GlobalConfig(GlobalConfig&&) = default;

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

    enum
    {
        CURSOR_TIMEOUT_DEFAULT = 60 // seconds
    };

    std::string           user;
    std::string           password;
    OnUnknownCommand      on_unknown_command      { RETURN_ERROR };
    bool                  auto_create_databases   { true };
    bool                  auto_create_tables      { true };
    int64_t               id_length               { ID_LENGTH_DEFAULT };
    OrderedInsertBehavior ordered_insert_behavior { OrderedInsertBehavior::DEFAULT };
    std::chrono::seconds  cursor_timeout          { std::chrono::seconds(CURSOR_TIMEOUT_DEFAULT) };

    static mxs::config::Specification& specification();

    static mxs::config::ParamString                      s_user;
    static mxs::config::ParamString                      s_password;
    static mxs::config::ParamEnum<OnUnknownCommand>      s_on_unknown_command;
    static mxs::config::ParamBool                        s_auto_create_databases;
    static mxs::config::ParamBool                        s_auto_create_tables;
    static mxs::config::ParamCount                       s_id_length;
    static mxs::config::ParamEnum<OrderedInsertBehavior> s_ordered_insert_behavior;
    static mxs::config::ParamSeconds                     s_cursor_timeout;
};

class Config final
{
public:
    Config(const GlobalConfig& config)
        : user(config.user)
        , password(config.password)
        , on_unknown_command(config.on_unknown_command)
        , auto_create_databases(config.auto_create_databases)
        , auto_create_tables(config.auto_create_tables)
        , id_length(config.id_length)
        , ordered_insert_behavior(config.ordered_insert_behavior)
        , cursor_timeout(config.cursor_timeout)
    {
    }

    const std::string                   user;
    const std::string                   password;
    GlobalConfig::OnUnknownCommand      on_unknown_command;
    bool                                auto_create_databases;
    bool                                auto_create_tables;
    int64_t                             id_length;
    GlobalConfig::OrderedInsertBehavior ordered_insert_behavior;
    std::chrono::seconds                cursor_timeout;
};

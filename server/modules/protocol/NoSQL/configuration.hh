/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include <maxscale/config2.hh>

class ProtocolModule;

class Configuration final : public mxs::config::Configuration
{
public:
    Configuration(const std::string& name, ProtocolModule* pInstance);

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

    enum
    {
        CURSOR_TIMEOUT_DEFAULT = 60     // seconds
    };

    // Can only be changed via MaxScale
    std::string           user;
    std::string           password;
    std::string           host;
    bool                  authentication_required;
    bool                  authorization_enabled;
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
    static mxs::config::ParamBool                        s_authentication_required;
    static mxs::config::ParamBool                        s_authorization_enabled;
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
    ProtocolModule& m_instance;
};

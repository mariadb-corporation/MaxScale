/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#if !defined (MXS_MODULE_NAME)
#define MXS_MODULE_NAME "dbfwfilter"
#endif
#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>

/**
 * Possible actions to take when the query matches a rule
 */
enum fw_actions
{
    FW_ACTION_ALLOW,
    FW_ACTION_BLOCK,
    FW_ACTION_IGNORE
};

class DbfwConfig : public mxs::config::Configuration
{
public:
    DbfwConfig(const DbfwConfig&) = delete;
    DbfwConfig& operator=(const DbfwConfig&) = delete;

    DbfwConfig(const std::string& name);

    DbfwConfig(DbfwConfig&& rhs) = default;

    static void populate(MXS_MODULE& module);

    std::string rules;
    bool        log_match;
    bool        log_no_match;
    bool        treat_string_as_field;
    bool        treat_string_arg_as_field;
    bool        strict;
    fw_actions  action;
};

/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/workerlocal.hh>
#include <string>

class MaskingFilter;
class MaskingRules;

class MaskingFilterConfig : public mxs::config::Configuration
{
public:
    MaskingFilterConfig(const MaskingFilterConfig&) = delete;
    MaskingFilterConfig& operator=(const MaskingFilterConfig&) = delete;

    enum warn_type_mismatch_t
    {
        WARN_NEVER,
        WARN_ALWAYS
    };

    enum large_payload_t
    {
        LARGE_IGNORE,
        LARGE_ABORT
    };

    MaskingFilterConfig(const char* zName, MaskingFilter& filter);

    MaskingFilterConfig(MaskingFilterConfig&&) = default;

    ~MaskingFilterConfig()
    {
    }

    struct Values
    {
        large_payload_t      large_payload;
        std::string          rules;
        warn_type_mismatch_t warn_type_mismatch;
        bool                 prevent_function_usage;
        bool                 check_user_variables;
        bool                 check_unions;
        bool                 check_subqueries;
        bool                 require_fully_parsed;
        bool                 treat_string_arg_as_field;

        std::shared_ptr<MaskingRules> sRules;

        bool is_parsing_needed() const
        {
            return prevent_function_usage || check_user_variables || check_unions || check_subqueries;
        }
    };

    const Values& values() const
    {
        return *m_values;
    }

    static void populate(MXS_MODULE& info);

    bool reload_rules();

protected:
    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;

private:
    MaskingFilter& m_filter;

    Values                    m_v;
    mxs::WorkerGlobal<Values> m_values;
};

/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include <string>
#include <regex.h>

#include <maxscale/filter.hh>
#include <maxscale/service.hh>
#include <maxscale/config2.hh>

#include "teesession.hh"

/**
 * The instance structure for the TEE filter - this holds the configuration
 * information for the filter.
 */
class Tee : public mxs::Filter
{
    Tee(const Tee&);
    const Tee& operator=(const Tee&);
public:

    struct Config : public mxs::config::Configuration
    {
        Config(const char* name);

        mxs::Target*            target;
        SERVICE*                service;
        std::string             user;   /* The user name to filter on */
        std::string             source; /* The source of the client connection */
        mxs::config::RegexValue match;  /* Compiled match pattern */
        mxs::config::RegexValue exclude;/* Compiled exclude pattern*/

        bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;
    };

    static Tee* create(const char* zName);
    TeeSession* newSession(MXS_SESSION* session, SERVICE* service) override;
    json_t*     diagnostics() const override;

    uint64_t getCapabilities() const override
    {
        return RCAP_TYPE_STMT_INPUT;
    }

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    bool user_matches(const char* user) const
    {
        return m_config.user.length() == 0 || strcmp(user, m_config.user.c_str()) == 0;
    }

    bool remote_matches(const char* remote) const
    {
        return m_config.source.length() == 0 || strcmp(remote, m_config.source.c_str()) == 0;
    }

    mxs::Target* get_target() const
    {
        return m_config.target;
    }

    const mxb::Regex& get_match() const
    {
        return m_config.match;
    }

    const mxb::Regex& get_exclude() const
    {
        return m_config.exclude;
    }

    void set_enabled(bool value)
    {
        m_enabled = value;
    }

    bool is_enabled() const
    {
        return m_enabled;
    }

private:
    Tee(const char* name);

    std::string m_name;
    Config      m_config;
    bool        m_enabled;
};

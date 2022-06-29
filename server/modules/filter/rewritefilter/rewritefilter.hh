/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include "rewritesession.hh"
#include "template_reader.hh"

#include <maxscale/config2.hh>
#include <maxscale/filter.hh>

#include <memory>

class RewriteFilterSession;

struct Settings
{
    bool                     nocase;
    std::string              template_file;
    std::vector<TemplateDef> templates;
};

class RewriteFilter : public mxs::Filter
{
public:
    RewriteFilter(const RewriteFilter&) = delete;
    RewriteFilter& operator=(const RewriteFilter&) = delete;

    static RewriteFilter* create(const char* zName);

    RewriteFilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService) override;

    json_t* diagnostics() const override;

    uint64_t getCapabilities() const override;

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

private:
    RewriteFilter(const std::string& name);

private:
    class Config : public mxs::config::Configuration
    {
    public:
        Config(const std::string& name, RewriteFilter& filter);

    private:
        // Calls RewriteFilter::set_settings()
        bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override final;

        RewriteFilter& m_filter;
        Settings       m_settings;
    };

    // Thread-safe set and get of current settings.
    void                      set_settings(std::unique_ptr<Settings> settings);
    std::shared_ptr<Settings> get_settings() const;

    Config                    m_config;
    mutable std::mutex        m_settings_mutex;
    std::shared_ptr<Settings> m_sSettings;
};

/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

#include <maxbase/ccdefs.hh>
#include "rewritesession.hh"
#include "template_reader.hh"
#include "sql_rewriter.hh"

#include <maxscale/config2.hh>
#include <maxscale/filter.hh>
#include <maxscale/protocol/mariadb/module_names.hh>

#include <memory>

class RewriteFilterSession;

struct Settings
{
    bool                     reload = false;
    bool                     case_sensitive = true;
    bool                     log_replacement = false;
    RegexGrammar             regex_grammar;
    std::string              template_file;
    std::vector<TemplateDef> templates;
};

struct SessionData
{
    SessionData(const Settings& settings, std::vector<std::unique_ptr<SqlRewriter>>&& rewriters)
        : settings(settings)
        , rewriters(std::move(rewriters))
    {
    }

    Settings                                  settings;
    std::vector<std::unique_ptr<SqlRewriter>> rewriters;
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

    std::set<std::string> protocols() const override
    {
        return {MXS_MARIADB_PROTOCOL_NAME};
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
        bool post_configure(const std::map<std::string, mxs::ConfigParameters>&) override final;

        RewriteFilter& m_filter;
        Settings       m_settings;
        // Don't warn if this is the first time, the filter will not be created
        // and plenty of errors will be logged.
        bool m_warn_bad_config = false;
    };

    // Thread-safe set and get of session data.
    void                               set_session_data(std::unique_ptr<const SessionData> s);
    std::shared_ptr<const SessionData> get_session_data() const;

    Config                             m_config;
    mutable std::mutex                 m_settings_mutex;
    std::shared_ptr<const SessionData> m_sSession_data;
};

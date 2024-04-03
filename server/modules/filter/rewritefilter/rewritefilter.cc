/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "rewritefilter"
#include "rewritefilter.hh"
#include "native_rewriter.hh"
#include "regex_rewriter.hh"
#include <string>

using std::string;
namespace cfg = mxs::config;

namespace
{
namespace rewritefilter
{

cfg::Specification specification(MXB_MODULE_NAME, cfg::Specification::FILTER);

// This config parameter is meant to be used as a configuration reload trigger.
// Setting it to true causes a reload, post_configure() sets it back to false.
cfg::ParamBool reload(
    &specification, "reload", "Reload configuration", false, cfg::Param::AT_RUNTIME);

cfg::ParamBool case_sensitive(
    &specification, "case_sensitive", "Matching default case sensitivity", true, cfg::Param::AT_RUNTIME);

cfg::ParamPath template_file(
    &specification, "template_file", "templates", cfg::ParamPath::R, cfg::Param::AT_RUNTIME);

cfg::ParamBool log_replacement(
    &specification, "log_replacement", "Log replacements at INFO level", false, cfg::Param::AT_RUNTIME);

cfg::ParamEnum<RegexGrammar> regex_grammar(
    &specification, "regex_grammar", "Regex grammar, or Native for the Rewrite filter native syntax",
        {
            {RegexGrammar::Native, "Native"},
            {RegexGrammar::ECMAScript, "ECMAScript"},
            {RegexGrammar::Posix, "Posix"},
            {RegexGrammar::EPosix, "Extended_posix"},
            {RegexGrammar::Awk, "Awk"},
            {RegexGrammar::Grep, "Grep"},
            {RegexGrammar::EGrep, "EGrep"},
        },
    RegexGrammar::Native,
    cfg::Param::AT_RUNTIME
    );
}
}

static uint64_t CAPABILITIES = RCAP_TYPE_STMT_INPUT;

//
// Global symbols of the Module
//
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "Rewrite filter.",
        "V1.0.0",
        CAPABILITIES,
        &mxs::FilterApi<RewriteFilter>::s_api,
        nullptr,    /* Process init. */
        nullptr,    /* Process finish. */
        nullptr,    /* Thread init. */
        nullptr,    /* Thread finish. */
        &rewritefilter::specification
    };

    return &info;
}

//
// RewriteFilter
//

RewriteFilter::RewriteFilter::Config::Config(const std::string& name, RewriteFilter& filter)
    : cfg::Configuration(name, &rewritefilter::specification)
    , m_filter(filter)

{
    add_native(&Config::m_settings, &Settings::reload, &rewritefilter::reload);
    add_native(&Config::m_settings, &Settings::case_sensitive, &rewritefilter::case_sensitive);
    add_native(&Config::m_settings, &Settings::log_replacement, &rewritefilter::log_replacement);
    add_native(&Config::m_settings, &Settings::template_file, &rewritefilter::template_file);
    add_native(&Config::m_settings, &Settings::regex_grammar, &rewritefilter::regex_grammar);
}

bool RewriteFilter::Config::post_configure(const std::map<std::string, maxscale::ConfigParameters>&)
{
    bool ok = true;
    try
    {
        TemplateDef default_template {m_settings.case_sensitive, m_settings.regex_grammar};
        std::vector<std::unique_ptr<SqlRewriter>> rewriters;

        if (!m_settings.template_file.empty())
        {
            TemplateReader reader(m_settings.template_file, default_template);
            m_settings.templates = reader.templates();
            rewriters = create_rewriters(m_settings.templates);
        }

        m_filter.set_session_data(std::make_unique<SessionData>(m_settings, std::move(rewriters)));
    }
    catch (const std::exception& ex)
    {
        MXB_SERROR(ex.what());
        if (m_warn_bad_config)
        {
            MXB_SERROR("Invalid config. Keeping current config unchanged.");
        }

        m_warn_bad_config = true;
        m_settings.reload = false;
        ok = false;
    }

    return ok;
}

RewriteFilter::RewriteFilter(const std::string& name)
    : m_config(name, *this)
{
}

void RewriteFilter::set_session_data(std::unique_ptr<const SessionData> s)
{
    std::lock_guard<std::mutex> guard(m_settings_mutex);
    m_sSession_data = std::move(s);
}

std::shared_ptr<const SessionData> RewriteFilter::get_session_data() const
{
    std::lock_guard<std::mutex> guard(m_settings_mutex);
    return m_sSession_data;
}

// static
RewriteFilter* RewriteFilter::create(const char* zName)
{
    return new RewriteFilter(zName);
}

std::shared_ptr<mxs::FilterSession> RewriteFilter::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    return std::shared_ptr<mxs::FilterSession>(
        RewriteFilterSession::create(pSession, pService, get_session_data()));
}

// static
json_t* RewriteFilter::diagnostics() const
{
    return m_config.to_json();
}

uint64_t RewriteFilter::getCapabilities() const
{
    return CAPABILITIES;
}

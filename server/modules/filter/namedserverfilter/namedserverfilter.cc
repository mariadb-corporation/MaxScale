/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file namedserverfilter.cc - a very simple regular expression based filter
 * that routes to a named server or server type if a regular expression match
 * is found.
 * @verbatim
 *
 * A simple regular expression based query routing filter.
 * Two parameters should be defined in the filter configuration
 *      match=<regular expression>
 *      server=<server to route statement to>
 * Two optional parameters
 *      source=<source address to limit filter>
 *      user=<username to limit filter>
 *
 * @endverbatim
 */

#define MXB_MODULE_NAME "namedserverfilter"

#include "namedserverfilter.hh"

#include <stdio.h>
#include <string>
#include <string.h>
#include <vector>

#include <maxscale/hint.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/server.hh>
#include <maxscale/session.hh>
#include <maxscale/config2.hh>

using std::string;

namespace
{

namespace cfg = mxs::config;
using ParamString = mxs::config::ParamString;
using ParamRegex = mxs::config::ParamRegex;
auto su = cfg::Param::AT_RUNTIME;

class Specification final : public cfg::Specification
{
public:
    using cfg::Specification::Specification;

private:
    template<class Params>
    bool do_post_validate(Params& params) const;

    bool post_validate(const cfg::Configuration* config,
                       const mxs::ConfigParameters& params,
                       const std::map<std::string, mxs::ConfigParameters>& nested_params) const override final
    {
        return do_post_validate(params);
    }

    bool post_validate(const cfg::Configuration* config,
                       json_t* json,
                       const std::map<std::string, json_t*>& nested_params) const override final
    {
        return do_post_validate(json);
    }
};

class ParamHintTarget : public cfg::ParamString
{
public:
    using cfg::ParamString::ParamString;

    std::vector<std::string> get_dependencies(const std::string& value) const override final
    {
        std::vector<std::string> deps;

        if (value != "->master" && value != "->slave" && value != "->all")
        {
            deps = config_break_list_string(value);
        }

        return deps;
    }
};

Specification s_spec(MXB_MODULE_NAME, cfg::Specification::FILTER);

ParamString s_user(&s_spec, "user", "Only divert queries from this user", "", su);
ParamString s_source(&s_spec, "source", "Only divert queries from these addresses", "", su);

const std::vector<std::pair<uint32_t, const char*>> options_values = {
    {PCRE2_CASELESS, "ignorecase"},
    {0,              "case"      },
    {PCRE2_EXTENDED, "extended"  }};
cfg::ParamEnumMask<uint32_t> s_options(&s_spec, "options", "Regular expression options",
                                       options_values, PCRE2_CASELESS, su);
// Legacy parameters
const char regex_desc[] = "Regular expression to match";
ParamString s_match(&s_spec, "match", regex_desc, "");
ParamHintTarget s_server(&s_spec, "server", "Server to divert matching queries", "");

// Indexed parameters
const char target_desc[] = "Target to divert matching queries";
ParamRegex s_match01(&s_spec, "match01", regex_desc, "", su);
ParamHintTarget s_target01(&s_spec, "target01", target_desc, "", su);

ParamRegex s_match02(&s_spec, "match02", regex_desc, "", su);
ParamHintTarget s_target02(&s_spec, "target02", target_desc, "", su);

ParamRegex s_match03(&s_spec, "match03", regex_desc, "", su);
ParamHintTarget s_target03(&s_spec, "target03", target_desc, "", su);

ParamRegex s_match04(&s_spec, "match04", regex_desc, "", su);
ParamHintTarget s_target04(&s_spec, "target04", target_desc, "", su);

ParamRegex s_match05(&s_spec, "match05", regex_desc, "", su);
ParamHintTarget s_target05(&s_spec, "target05", target_desc, "", su);

ParamRegex s_match06(&s_spec, "match06", regex_desc, "", su);
ParamHintTarget s_target06(&s_spec, "target06", target_desc, "", su);

ParamRegex s_match07(&s_spec, "match07", regex_desc, "", su);
ParamHintTarget s_target07(&s_spec, "target07", target_desc, "", su);

ParamRegex s_match08(&s_spec, "match08", regex_desc, "", su);
ParamHintTarget s_target08(&s_spec, "target08", target_desc, "", su);

ParamRegex s_match09(&s_spec, "match09", regex_desc, "", su);
ParamHintTarget s_target09(&s_spec, "target09", target_desc, "", su);

ParamRegex s_match10(&s_spec, "match10", regex_desc, "", su);
ParamHintTarget s_target10(&s_spec, "target10", target_desc, "", su);

ParamRegex s_match11(&s_spec, "match11", regex_desc, "", su);
ParamHintTarget s_target11(&s_spec, "target11", target_desc, "", su);

ParamRegex s_match12(&s_spec, "match12", regex_desc, "", su);
ParamHintTarget s_target12(&s_spec, "target12", target_desc, "", su);

ParamRegex s_match13(&s_spec, "match13", regex_desc, "", su);
ParamHintTarget s_target13(&s_spec, "target13", target_desc, "", su);

ParamRegex s_match14(&s_spec, "match14", regex_desc, "", su);
ParamHintTarget s_target14(&s_spec, "target14", target_desc, "", su);

ParamRegex s_match15(&s_spec, "match15", regex_desc, "", su);
ParamHintTarget s_target15(&s_spec, "target15", target_desc, "", su);

ParamRegex s_match16(&s_spec, "match16", regex_desc, "", su);
ParamHintTarget s_target16(&s_spec, "target16", target_desc, "", su);

ParamRegex s_match17(&s_spec, "match17", regex_desc, "", su);
ParamHintTarget s_target17(&s_spec, "target17", target_desc, "", su);

ParamRegex s_match18(&s_spec, "match18", regex_desc, "", su);
ParamHintTarget s_target18(&s_spec, "target18", target_desc, "", su);

ParamRegex s_match19(&s_spec, "match19", regex_desc, "", su);
ParamHintTarget s_target19(&s_spec, "target19", target_desc, "", su);

ParamRegex s_match20(&s_spec, "match20", regex_desc, "", su);
ParamHintTarget s_target20(&s_spec, "target20", target_desc, "", su);

ParamRegex s_match21(&s_spec, "match21", regex_desc, "", su);
ParamHintTarget s_target21(&s_spec, "target21", target_desc, "", su);

ParamRegex s_match22(&s_spec, "match22", regex_desc, "", su);
ParamHintTarget s_target22(&s_spec, "target22", target_desc, "", su);

ParamRegex s_match23(&s_spec, "match23", regex_desc, "", su);
ParamHintTarget s_target23(&s_spec, "target23", target_desc, "", su);

ParamRegex s_match24(&s_spec, "match24", regex_desc, "", su);
ParamHintTarget s_target24(&s_spec, "target24", target_desc, "", su);

ParamRegex s_match25(&s_spec, "match25", regex_desc, "", su);
ParamHintTarget s_target25(&s_spec, "target25", target_desc, "", su);

struct MatchAndTarget
{
    ParamRegex*      match {nullptr};
    ParamHintTarget* target {nullptr};
};
std::vector<MatchAndTarget> s_match_target_specs = {
    {&s_match01, &s_target01}, {&s_match02, &s_target02},
    {&s_match03, &s_target03}, {&s_match04, &s_target04},
    {&s_match05, &s_target05}, {&s_match06, &s_target06},
    {&s_match07, &s_target07}, {&s_match08, &s_target08},
    {&s_match09, &s_target09}, {&s_match10, &s_target10},
    {&s_match11, &s_target11}, {&s_match12, &s_target12},
    {&s_match13, &s_target13}, {&s_match14, &s_target14},
    {&s_match15, &s_target15}, {&s_match16, &s_target16},
    {&s_match17, &s_target17}, {&s_match18, &s_target18},
    {&s_match19, &s_target19}, {&s_match20, &s_target20},
    {&s_match21, &s_target21}, {&s_match22, &s_target22},
    {&s_match23, &s_target23}, {&s_match24, &s_target24},
    {&s_match25, &s_target25}};

template<class Params>
bool Specification::do_post_validate(Params& params) const
{
    bool ok = true;

    std::string legacy_match = s_match.get(params);
    std::string legacy_target = s_server.get(params);
    bool legacy_mode = false;
    bool found = false;

    if (legacy_match.empty() != legacy_target.empty())
    {
        MXB_ERROR("Only one of '%s' and '%s' is set. If using legacy mode, set both."
                  "If using indexed parameters, set neither and use '%s01' and '%s01' etc.",
                  s_match.name().c_str(), s_server.name().c_str(),
                  s_match01.name().c_str(), s_target01.name().c_str());
        ok = false;
    }
    else if (!legacy_match.empty())
    {
        legacy_mode = true;

        mxb::Regex re(legacy_match);

        if (!re.valid())
        {
            MXB_ERROR("Invalid regular expression for '%s': %s",
                      s_match.name().c_str(), re.error().c_str());
            ok = false;
        }

        if (!mxs::Target::find(legacy_target))
        {
            MXB_ERROR("'%s' is not a valid value for '%s'",
                      legacy_target.c_str(), s_server.name().c_str());
            ok = false;
        }
    }

    for (const auto& a : s_match_target_specs)
    {
        std::string match = a.match->get(params).pattern();
        std::string target = a.target->get(params);

        if (match.empty() && target.empty())
        {
            continue;
        }
        else if (!match.empty() && !target.empty())
        {
            if (legacy_mode)
            {
                MXB_ERROR("Found both legacy parameters and indexed parameters. "
                          "Use only one type of parameters.");
                ok = false;
                break;
            }

            found = true;

            auto targets = config_break_list_string(target);

            if (targets.size() == 1)
            {
                if (!mxs::Target::find(targets[0])
                    && targets[0] != "->master"
                    && targets[0] != "->slave"
                    && targets[0] != "->all")
                {
                    MXB_ERROR("'%s' is not a valid value for '%s'",
                              targets[0].c_str(), a.target->name().c_str());
                    ok = false;
                }
            }
            else if (targets.size() > 1)
            {
                for (const auto& t : targets)
                {
                    if (!mxs::Target::find(t))
                    {
                        MXB_ERROR("'%s' is not a valid value for '%s': %s",
                                  targets[0].c_str(), a.target->name().c_str(), target.c_str());
                        ok = false;
                    }
                }
            }
            else
            {
                MXB_ERROR("Invalid target string for '%s': %s",
                          a.target->name().c_str(), target.c_str());
                ok = false;
            }

            mxb::Regex re(match);

            if (!re.valid())
            {
                MXB_ERROR("Invalid regular expression for '%s': %s",
                          a.match->name().c_str(), re.error().c_str());
                ok = false;
            }
        }
        else
        {
            const auto& defined = match.empty() ? a.target->name() : a.match->name();
            const auto& not_defined = match.empty() ? a.match->name() : a.target->name();
            MXB_ERROR("'%s' does not have a matching '%s'.", defined.c_str(), not_defined.c_str());
            ok = false;
        }
    }

    if (ok && !found && !legacy_mode)
    {
        MXB_ERROR("At least one match-target pair must be defined.");
        ok = false;
    }

    return ok;
}
}

RegexHintFSession::RegexHintFSession(MXS_SESSION* session, SERVICE* service, RegexHintFilter& filter,
                                     bool active, std::shared_ptr<RegexHintFilter::Setup>&& setup)
    : maxscale::FilterSession::FilterSession(session, service)
    , m_fil_inst(filter)
    , m_active(active)
    , m_setup(std::move(setup))
{
    m_match_data = pcre2_match_data_create(m_setup->ovector_size, NULL);
}

RegexHintFSession::~RegexHintFSession()
{
    pcre2_match_data_free(m_match_data);
}

/**
 * If the regular expression configured in the match parameter of the
 * filter definition matches the SQL text then add the hint
 * "Route to named server" with the name defined in the regex-server mapping
 *
 * @param queue     The query data
 * @return True on success, false on failure
 */
bool RegexHintFSession::routeQuery(GWBUF&& buffer)
{
    if (m_active)
    {
        std::string_view sv = parser().get_sql(buffer);
        if (!sv.empty())
        {
            const char* sql = sv.data();
            int sql_len = sv.length();

            // Is either a COM_QUERY or COM_STMT_PREPARE. In either case, generate hints.
            const RegexToServers* reg_serv = find_servers(sql, sql_len);
            auto cmd = mariadb::get_command(buffer);
            switch (cmd)
            {
            case MXS_COM_QUERY:
                // A normal query. If a mapping was found, add hints to the buffer.
                inc_diverted(reg_serv);

                if (reg_serv)
                {
                    for (const auto& target : reg_serv->m_targets)
                    {
                        buffer.add_hint(reg_serv->m_htype, target);
                    }
                }

                break;

            case MXS_COM_STMT_PREPARE:
                {
                    // Not adding any hints to the prepare command itself as it should be routed normally.
                    // Instead, save the id and hints so that execution of the PS can be properly hinted.
                    if (reg_serv)
                    {
                        // The PS ID is the id of the buffer. This is set by client protocol and should be
                        // used all over the routing chain.
                        uint32_t ps_id = buffer.id();
                        // Replacing an existing hint list is ok, although this should not happen as long
                        // as the PS IDs are unique.
                        auto& hints = m_ps_id_to_hints[ps_id];
                        hints.clear();

                        Hint::Type htype = reg_serv->m_htype;
                        const auto& targets = reg_serv->m_targets;
                        hints.reserve(targets.size());
                        for (const auto& target : targets)
                        {
                            hints.emplace_back(htype, target);
                        }

                        // So far we have assumed that the preparation will succeed. In case it won't, the map
                        // entry will be removed in clientReply.
                        m_current_prep_id = ps_id;
                        m_last_prepare_id = ps_id;
                    }
                }
                break;

            default:
                mxb_assert(!true);
            }
        }
        else if (buffer.length() >= 9)
        {
            // Can be a PS command with ID.
            auto cmd = mariadb::get_command(buffer);
            switch (cmd)
            {
            case MXS_COM_STMT_EXECUTE:
            case MXS_COM_STMT_BULK_EXECUTE:
            case MXS_COM_STMT_SEND_LONG_DATA:
                {
                    uint32_t ps_id = mxs_mysql_extract_ps_id(buffer);
                    // -1 means use the last prepared stmt.
                    if (ps_id == MARIADB_PS_DIRECT_EXEC_ID && m_last_prepare_id > 0)
                    {
                        ps_id = m_last_prepare_id;
                    }

                    auto it = m_ps_id_to_hints.find(ps_id);
                    inc_diverted(it != m_ps_id_to_hints.end());

                    if (it != m_ps_id_to_hints.end())
                    {
                        for (const auto& new_hint : it->second)
                        {
                            buffer.add_hint(new_hint);
                        }
                    }
                }
                break;

            case MXS_COM_STMT_CLOSE:
                {
                    uint32_t ps_id = mxs_mysql_extract_ps_id(buffer);
                    m_ps_id_to_hints.erase(ps_id);
                }
                break;

            default:
                break;
            }
        }
    }

    return FilterSession::routeQuery(std::move(buffer));
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param session   The client session to attach to
 * @return a new filter session
 */
std::shared_ptr<mxs::FilterSession> RegexHintFilter::newSession(MXS_SESSION* session, SERVICE* service)
{
    bool session_active = true;
    bool ip_found = false;
    auto& sett = m_settings;
    /* Check client IP against 'source' host option */
    auto& remote = session->client_remote();
    sockaddr_storage remote_addr;
    mxb::get_normalized_ip(session->client_eff_addr(), &remote_addr);

    auto setup = *m_setup;
    if (!setup->sources.empty())
    {
        ip_found = check_source_host(setup, remote.c_str(), &remote_addr);
        session_active = ip_found;
    }
    /* Don't check hostnames if ip is already found */
    if (!setup->hostnames.empty() && !ip_found)
    {
        session_active = check_source_hostnames(setup, &remote_addr);
    }

    /* Check client user against 'user' option */
    if (!sett.m_user.empty() && (sett.m_user != session->user()))
    {
        session_active = false;
    }
    return std::make_shared<RegexHintFSession>(session, service, *this, session_active, std::move(setup));
}

/**
 * Find the first server list with a matching regular expression.
 *
 * @param sql   SQL-query string, not null-terminated
 * @param sql_len   length of SQL-query
 * @return A set of servers from the main mapping container
 */
const RegexToServers* RegexHintFSession::find_servers(const char* sql, int sql_len)
{
    /* Go through the regex array and find a match. */
    for (auto& regex_map : m_setup->mapping)
    {
        pcre2_code* regex = regex_map.m_regex;
        int result = pcre2_match(regex, (PCRE2_SPTR)sql, sql_len, 0, 0, m_match_data, nullptr);
        if (result >= 0)
        {
            /* Have a match. No need to check if the regex matches the complete
             * query, since the user can form the regex to enforce this. */
            return &(regex_map);
        }
        else if (result != PCRE2_ERROR_NOMATCH)
        {
            /* Error during matching */
            if (!regex_map.m_error_printed.load(std::memory_order_relaxed))
            {
                MXS_PCRE2_PRINT_ERROR(result);
                regex_map.m_error_printed.store(true, std::memory_order_relaxed);
            }
            return NULL;
        }
    }
    return NULL;
}

void RegexHintFSession::inc_diverted(bool was_diverted)
{
    if (was_diverted)
    {
        m_n_diverted++;
        m_fil_inst.m_total_diverted.fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        m_n_undiverted++;
        m_fil_inst.m_total_undiverted.fetch_add(1, std::memory_order_relaxed);
    }
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
uint64_t RegexHintFilter::getCapabilities() const
{
    return RCAP_TYPE_STMT_INPUT;
}

mxs::config::Configuration& RegexHintFilter::getConfiguration()
{
    return m_settings;
}

/**
 * Create an instance of the filter
 *
 * @param name  Filter instance name
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The new instance or null on error
 */
std::unique_ptr<mxs::Filter> RegexHintFilter::create(const char* name)
{
    return std::make_unique<RegexHintFilter>(name);
}

/**
 * Diagnostics routine
 *
 * Print diagnostics on the filter instance as a whole + session-specific info.
 */
json_t* RegexHintFSession::diagnostics() const
{
    json_t* rval = m_fil_inst.diagnostics();    /* Print overall diagnostics */

    json_object_set_new(rval, "session_queries_diverted", json_integer(m_n_diverted));
    json_object_set_new(rval, "session_queries_undiverted", json_integer(m_n_undiverted));

    return rval;
}

bool RegexHintFSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    if (reply.is_complete() && m_current_prep_id > 0)
    {
        if (reply.error())
        {
            // Preparation failed, remove from map.
            m_ps_id_to_hints.erase(m_current_prep_id);
            m_last_prepare_id = 0;
        }
        m_current_prep_id = 0;
    }
    return mxs::FilterSession::clientReply(std::move(packet), down, reply);
}

/**
 * Diagnostics routine
 *
 * Print diagnostics on the filter instance as a whole.
 *
 * @param   dcb     The DCB for diagnostic output
 */
json_t* RegexHintFilter::diagnostics() const
{
    json_t* rval = json_object();

    json_object_set_new(rval, "queries_diverted", json_integer(m_total_diverted.load()));
    json_object_set_new(rval, "queries_undiverted", json_integer(m_total_undiverted.load()));

    auto setup = *m_setup;

    if (setup->mapping.size() > 0)
    {
        json_t* arr = json_array();

        for (const auto& regex_map : setup->mapping)
        {
            json_t* obj = json_object();
            json_t* targets = json_array();

            for (const auto& target : regex_map.m_targets)
            {
                json_array_append_new(targets, json_string(target.c_str()));
            }

            json_object_set_new(obj, "match", json_string(regex_map.m_match.c_str()));
            json_object_set_new(obj, "targets", targets);
            json_array_append_new(arr, obj);
        }

        json_object_set_new(rval, "mappings", arr);
    }

    if (!setup->sources.empty())
    {
        json_t* arr = json_array();

        for (const auto& source : setup->sources)
        {
            json_array_append_new(arr, json_string(source.m_address.c_str()));
        }
        json_object_set_new(rval, "sources", arr);
    }

    if (!m_settings.m_user.empty())
    {
        json_object_set_new(rval, "user", json_string(m_settings.m_user.c_str()));
    }

    return rval;
}

/**
 * Parse the target list and add the elements to the internal list. Server names are verified.
 *
 * @param target Routing target as read from the config file
 * @param legacy_mode Using legacy mode for backwards compatibility?
 * @return True on success
 */
bool RegexToServers::add_targets(const std::string& target, bool legacy_mode)
{
    if (legacy_mode)
    {
        /* Should have just one server name, already known to be valid */
        m_targets.push_back(target);
        return true;
    }

    /* Have to parse the server list here instead of in config loader, since the list
     * may contain special placeholder strings. */
    bool error = false;
    auto targets_array = config_break_list_string(target);
    if (targets_array.size() > 1)
    {
        for (const auto& elem : targets_array)
        {
            mxb_assert(mxs::Target::find(elem));
            m_targets.push_back(elem);
        }
    }
    else if (targets_array.size() == 1)
    {
        /* The string is either a server name or a special reserved id */
        auto& only_elem = targets_array[0];
        if (mxs::Target::find(only_elem))
        {
            m_targets.push_back(only_elem);
        }
        else if (only_elem == "->master")
        {
            m_targets.push_back(only_elem);
            m_htype = Hint::Type::ROUTE_TO_MASTER;
        }
        else if (only_elem == "->slave")
        {
            m_targets.push_back(only_elem);
            m_htype = Hint::Type::ROUTE_TO_SLAVE;
        }
        else if (only_elem == "->all")
        {
            m_targets.push_back(only_elem);
            m_htype = Hint::Type::ROUTE_TO_ALL;
        }
        else
        {
            // This should no longer happen
            mxb_assert(!true);
            error = true;
        }
    }
    else
    {
        // This should no longer happen
        mxb_assert(!true);
        // targets-list had no elements
        error = true;
    }

    return !error;
}

RegexToServers::RegexToServers(RegexToServers&& rhs) noexcept
    : m_match(std::move(rhs.m_match))
    , m_regex(rhs.m_regex)
    , m_targets(std::move(rhs.m_targets))
    , m_htype(rhs.m_htype)
{
    rhs.m_regex = nullptr;
    m_error_printed = rhs.m_error_printed.load();
}

RegexToServers::~RegexToServers()
{
    pcre2_code_free(m_regex);
}

bool RegexHintFilter::regex_compile_and_add(const std::shared_ptr<Setup>& setup,
                                            int pcre_ops, bool legacy_mode, const std::string& match,
                                            const std::string& target)
{
    bool success = true;
    int errorcode = -1;
    PCRE2_SIZE error_offset = -1;
    pcre2_code* regex = pcre2_compile((PCRE2_SPTR) match.c_str(), match.length(), pcre_ops,
                                      &errorcode, &error_offset, nullptr);

    if (regex)
    {
        // Try to compile even further for faster matching
        if (MXS_PCRE2_JIT_COMPILE(regex, PCRE2_JIT_COMPLETE) < 0)
        {
            MXB_NOTICE("PCRE2 JIT compilation of pattern '%s' failed, falling back to normal compilation.",
                       match.c_str());
        }

        RegexToServers mapping_elem(match, regex);
        if (mapping_elem.add_targets(target, legacy_mode))
        {
            setup->mapping.push_back(std::move(mapping_elem));

            /* Check what is the required match_data size for this pattern. The
             * largest value is used to form the match data. */
            uint32_t capcount = 0;
            int ret_info = pcre2_pattern_info(regex, PCRE2_INFO_CAPTURECOUNT, &capcount);
            if (ret_info != 0)
            {
                MXS_PCRE2_PRINT_ERROR(ret_info);
                success = false;
            }
            else
            {
                int required_ovec_size = capcount + 1;
                if (required_ovec_size > setup->ovector_size)
                {
                    setup->ovector_size = required_ovec_size;
                }
            }
        }
        else
        {
            // The targets string didn't seem to contain a valid routing target.
            MXB_ERROR("Could not parse a routing target from '%s'.", target.c_str());
            success = false;
        }
    }
    else
    {
        MXB_ERROR("Invalid PCRE2 regular expression '%s' (position '%zu').",
                  match.c_str(), error_offset);
        MXS_PCRE2_PRINT_ERROR(errorcode);
        mxb_assert_message(!true, "This should be detected earlier");
        success = false;
    }
    return success;
}

/**
 * Read all indexed regexes from the supplied configuration, compile them and form the mapping
 */
bool RegexHintFilter::form_regex_server_mapping(const std::shared_ptr<Setup>& setup, int pcre_ops)
{
    auto& regex_values = m_settings.m_match_targets;
    bool error = false;

    /* The config parameters can be in any order and may be skipping numbers. Go through all params and
     * save found ones to array. */
    std::vector<Settings::MatchAndTarget> found_pairs;

    for (size_t i = 0; i < RegexHintFilter::Settings::n_regex_max; i++)
    {
        auto& param_definition = s_match_target_specs[i];
        auto& param_name_match = param_definition.match->name();
        auto& param_name_target = param_definition.target->name();

        auto& param_val = regex_values[i];

        /* Check that both the matchXY and targetXY settings are found. */
        bool match_exists = !param_val.match.empty();
        bool target_exists = !param_val.target.empty();
        mxb_assert(match_exists == target_exists);

        if (match_exists && target_exists)
        {
            found_pairs.push_back(param_val);
        }
    }

    for (const auto& elem : found_pairs)
    {
        // TODO: Don't compile the patterns twice and use the RegexValue from the configuration
        if (!regex_compile_and_add(setup, pcre_ops, false, elem.match.pattern(), elem.target))
        {
            error = true;
        }
    }

    return !error;
}

/**
 * Check whether the client IP matches the configured 'source' host,
 * which can have up to three % wildcards.
 *
 * @param remote      The clientIP
 * @param ipv4        The client socket address struct
 * @return            true for match, false otherwise
 */
bool RegexHintFilter::check_source_host(const std::shared_ptr<Setup>& setup,
                                        const char* remote,
                                        const struct sockaddr_storage* ip)
{
    bool rval = false;
    struct sockaddr_storage addr;
    memcpy(&addr, ip, sizeof(addr));

    for (const auto& source : setup->sources)
    {
        rval = true;

        if (addr.ss_family == AF_INET6)
        {
            struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr;
            /* Check only bytes before netmask */
            for (int i = 0; i < source.m_netmask / 8; ++i)
            {
                if (addr6->sin6_addr.__in6_u.__u6_addr8[i] != source.m_ipv6.sin6_addr.__in6_u.__u6_addr8[i])
                {
                    rval = false;
                    break;
                }
            }
        }
        else if (addr.ss_family == AF_INET)
        {
            struct sockaddr_in* check_ipv4 = (struct sockaddr_in*)&addr;

            switch (source.m_netmask)
            {
            case 128:
                break;

            case 120:
                /* Class C check */
                check_ipv4->sin_addr.s_addr &= 0x00FFFFFF;
                break;

            case 112:
                /* Class B check */
                check_ipv4->sin_addr.s_addr &= 0x0000FFFF;
                break;

            case 104:
                /* Class A check */
                check_ipv4->sin_addr.s_addr &= 0x000000FF;
                break;

            default:
                break;
            }

            /* If source is mapped ipv4 address the actual ipv4 address is stored
             * in the last 4 bytes of ipv6 address. So lets compare that to the
             * client ipv4 address. */
            if (source.m_ipv6.sin6_addr.__in6_u.__u6_addr32[3] != check_ipv4->sin_addr.s_addr)
            {
                rval = false;
            }
        }

        if (rval)
        {
            MXB_INFO("Client IP %s matches host source %s%s",
                     remote,
                     source.m_netmask < 128 ? "with wildcards " : "",
                     source.m_address.c_str());
            return rval;
        }
    }

    return rval;
}

bool RegexHintFilter::check_source_hostnames(const std::shared_ptr<Setup>& setup,
                                             const struct sockaddr_storage* ip)
{
    struct sockaddr_storage addr;
    memcpy(&addr, ip, sizeof(addr));
    char hbuf[NI_MAXHOST];

    int rc = getnameinfo((struct sockaddr*)&addr, sizeof(addr), hbuf, sizeof(hbuf), NULL, 0, NI_NAMEREQD);

    if (rc != 0)
    {
        MXB_INFO("Failed to resolve hostname due to %s", gai_strerror(rc));
        return false;
    }

    for (const auto& host : setup->hostnames)
    {
        if (strcmp(hbuf, host.c_str()) == 0)
        {
            MXB_INFO("Client hostname %s matches host source %s", hbuf, host.c_str());
            return true;
        }
    }

    return false;
}

/**
 * Validate IP address string against three dots
 * and last char not being a dot.
 *
 * Match any, '%' or '%.%.%.%', is not allowed
 *
 */
bool RegexHintFilter::validate_ipv4_address(const char* host)
{
    int n_dots = 0;

    /**
     * Match any is not allowed
     * Start with dot not allowed
     * Host len can't be greater than INET_ADDRSTRLEN
     */
    if (*host == '%' || *host == '.' || strlen(host) > INET_ADDRSTRLEN)
    {
        return false;
    }

    /* Check each byte */
    while (*host != '\0')
    {

        if (!isdigit(*host) && *host != '.' && *host != '%')
        {
            return false;
        }

        /* Dot found */
        if (*host == '.')
        {
            n_dots++;
        }

        host++;
    }

    /* Check IPv4 max number of dots and last char */
    if (n_dots == 3 && (*(host - 1) != '.'))
    {
        return true;
    }
    else
    {
        return false;
    }
}

/**
 * Set the 'source' option into a proper struct. Input IP, which could have
 * wildcards %, is checked and the netmask 32/24/16/8 is added.
 *
 * @param input_host    The config source parameter
 * @return              The filled struct with netmask, or null on error
 */
bool RegexHintFilter::add_source_address(const std::shared_ptr<Setup>& setup, const std::string& input_host)
{
    std::string address(input_host);
    struct sockaddr_in6 ipv6 = {};
    int netmask = 128;
    std::string format_host = address;
    /* If no wildcards, leave netmask to 128 and return */
    if (strchr(input_host.c_str(), '%') && validate_ipv4_address(input_host.c_str()))
    {
        size_t pos = 0;
        while ((pos = format_host.find('%', pos)) != std::string::npos)
        {
            format_host.replace(pos, 1, "0");
            pos++;
            netmask -= 8;
        }
    }

    struct addrinfo* ai = NULL, hint = {};
    hint.ai_flags = AI_ADDRCONFIG | AI_NUMERICHOST | AI_V4MAPPED;
    hint.ai_family = AF_INET6;
    int rc = getaddrinfo(format_host.c_str(), NULL, &hint, &ai);

    /* fill IPv6 data struct */
    if (rc == 0)
    {
        memcpy(&ipv6, ai->ai_addr, ai->ai_addrlen);
        MXB_INFO("Input %s is valid with netmask %d", address.c_str(), netmask);
        freeaddrinfo(ai);
    }
    else
    {
        return false;
    }
    setup->sources.emplace_back(address, ipv6, netmask);
    return true;
}

void RegexHintFilter::set_source_addresses(const std::shared_ptr<Setup>& setup, const std::string& host_names)
{
    for (const auto& host : mxb::strtok(host_names, ","))
    {
        std::string trimmed_host = host;
        mxb::trim(trimmed_host);

        if (!add_source_address(setup, trimmed_host))
        {
            MXB_INFO("The given 'source' parameter '%s' is not a valid IP address. Adding it as hostname.",
                     trimmed_host.c_str());
            setup->hostnames.emplace_back(trimmed_host);
        }
    }
}

bool RegexHintFilter::post_configure()
{
    const char MATCH_STR[] = "match";
    const char SERVER_STR[] = "server";
    const char TARGET_STR[] = "target";

    auto setup = std::make_shared<Setup>();
    auto& sett = m_settings;

    if (!sett.m_source.empty())
    {
        set_source_addresses(setup, sett.m_source);
    }

    int pcre_ops = sett.m_regex_options;

    bool error = false;
    /* Try to form the mapping with indexed parameter names. */
    if (!form_regex_server_mapping(setup, pcre_ops))
    {
        error = true;
    }

    const bool legacy_mode = (!sett.m_match.empty() || !sett.m_server.empty());

    if (legacy_mode && setup->mapping.empty())
    {
        MXB_WARNING("Use of legacy parameters 'match' and 'server' is deprecated.");
        /* Using legacy mode and no indexed parameters found. Add the legacy parameters
         * to the mapping. */
        if (!regex_compile_and_add(setup, pcre_ops, true, sett.m_match, sett.m_server))
        {
            error = true;
        }
    }

    if (!error)
    {
        m_setup.assign(setup);
    }

    return !error;
}

RegexHintFilter::RegexHintFilter(const std::string& name)
    : m_settings(name, this)
{
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::GA,
        MXS_FILTER_VERSION,
        "A routing hint filter that uses regular expressions to direct queries",
        "V1.1.0",
        RCAP_TYPE_STMT_INPUT,
        &mxs::FilterApi<RegexHintFilter>::s_api,
        NULL,                                                                   /* Process init. */
        NULL,                                                                   /* Process finish. */
        NULL,                                                                   /* Thread init. */
        NULL,                                                                   /* Thread finish. */
        &s_spec
    };

    return &info;
}

RegexHintFilter::Settings::Settings(const string& name, RegexHintFilter* filter)
    : mxs::config::Configuration(name, &s_spec)
    , m_filter(filter)
{
    add_native(&Settings::m_user, &s_user);
    add_native(&Settings::m_source, &s_source);
    add_native(&Settings::m_regex_options, &s_options);

    add_native(&Settings::m_match, &s_match);
    add_native(&Settings::m_server, &s_server);

    mxb_assert(s_match_target_specs.size() == n_regex_max);
    for (int i = 0; i < n_regex_max; i++)
    {
        auto& value_store = m_match_targets[i];
        auto& spec = s_match_target_specs[i];

        add_native(&Settings::m_match_targets, i, &MatchAndTarget::match, spec.match);
        add_native(&Settings::m_match_targets, i, &MatchAndTarget::target, spec.target);
    }
}

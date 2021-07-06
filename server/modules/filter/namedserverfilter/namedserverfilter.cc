/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
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

#define MXS_MODULE_NAME "namedserverfilter"

#include "namedserverfilter.hh"

#include <stdio.h>
#include <string>
#include <string.h>
#include <vector>

#include <maxbase/alloc.h>
#include <maxscale/hint.h>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <maxscale/server.hh>
#include <maxscale/session.hh>
#include <maxscale/utils.h>
#include <maxscale/config2.hh>

using std::string;

namespace
{

namespace cfg = mxs::config;
using ParamString = mxs::config::ParamString;
auto su = cfg::Param::AT_STARTUP;
cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

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
ParamString s_match(&s_spec, "match", regex_desc, "", su);
ParamString s_server(&s_spec, "server", "Server to divert matching queries", "", su);

// Indexed parameters
const char target_desc[] = "Target to divert matching queries";
ParamString s_match01(&s_spec, "match01", regex_desc, "", su);
ParamString s_target01(&s_spec, "target01", target_desc, "", su);

ParamString s_match02(&s_spec, "match02", regex_desc, "", su);
ParamString s_target02(&s_spec, "target02", target_desc, "", su);

ParamString s_match03(&s_spec, "match03", regex_desc, "", su);
ParamString s_target03(&s_spec, "target03", target_desc, "", su);

ParamString s_match04(&s_spec, "match04", regex_desc, "", su);
ParamString s_target04(&s_spec, "target04", target_desc, "", su);

ParamString s_match05(&s_spec, "match05", regex_desc, "", su);
ParamString s_target05(&s_spec, "target05", target_desc, "", su);

ParamString s_match06(&s_spec, "match06", regex_desc, "", su);
ParamString s_target06(&s_spec, "target06", target_desc, "", su);

ParamString s_match07(&s_spec, "match07", regex_desc, "", su);
ParamString s_target07(&s_spec, "target07", target_desc, "", su);

ParamString s_match08(&s_spec, "match08", regex_desc, "", su);
ParamString s_target08(&s_spec, "target08", target_desc, "", su);

ParamString s_match09(&s_spec, "match09", regex_desc, "", su);
ParamString s_target09(&s_spec, "target09", target_desc, "", su);

ParamString s_match10(&s_spec, "match10", regex_desc, "", su);
ParamString s_target10(&s_spec, "target10", target_desc, "", su);

ParamString s_match11(&s_spec, "match11", regex_desc, "", su);
ParamString s_target11(&s_spec, "target11", target_desc, "", su);

ParamString s_match12(&s_spec, "match12", regex_desc, "", su);
ParamString s_target12(&s_spec, "target12", target_desc, "", su);

ParamString s_match13(&s_spec, "match13", regex_desc, "", su);
ParamString s_target13(&s_spec, "target13", target_desc, "", su);

ParamString s_match14(&s_spec, "match14", regex_desc, "", su);
ParamString s_target14(&s_spec, "target14", target_desc, "", su);

ParamString s_match15(&s_spec, "match15", regex_desc, "", su);
ParamString s_target15(&s_spec, "target15", target_desc, "", su);

ParamString s_match16(&s_spec, "match16", regex_desc, "", su);
ParamString s_target16(&s_spec, "target16", target_desc, "", su);

ParamString s_match17(&s_spec, "match17", regex_desc, "", su);
ParamString s_target17(&s_spec, "target17", target_desc, "", su);

ParamString s_match18(&s_spec, "match18", regex_desc, "", su);
ParamString s_target18(&s_spec, "target18", target_desc, "", su);

ParamString s_match19(&s_spec, "match19", regex_desc, "", su);
ParamString s_target19(&s_spec, "target19", target_desc, "", su);

ParamString s_match20(&s_spec, "match20", regex_desc, "", su);
ParamString s_target20(&s_spec, "target20", target_desc, "", su);

ParamString s_match21(&s_spec, "match21", regex_desc, "", su);
ParamString s_target21(&s_spec, "target21", target_desc, "", su);

ParamString s_match22(&s_spec, "match22", regex_desc, "", su);
ParamString s_target22(&s_spec, "target22", target_desc, "", su);

ParamString s_match23(&s_spec, "match23", regex_desc, "", su);
ParamString s_target23(&s_spec, "target23", target_desc, "", su);

ParamString s_match24(&s_spec, "match24", regex_desc, "", su);
ParamString s_target24(&s_spec, "target24", target_desc, "", su);

ParamString s_match25(&s_spec, "match25", regex_desc, "", su);
ParamString s_target25(&s_spec, "target25", target_desc, "", su);

struct MatchAndTarget
{
    ParamString* match {nullptr};
    ParamString* target {nullptr};
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
}

RegexHintFSession::RegexHintFSession(MXS_SESSION* session, SERVICE* service, RegexHintFilter& filter,
                                     bool active)
    : maxscale::FilterSession::FilterSession(session, service)
    , m_fil_inst(filter)
    , m_active(active)
{
    m_match_data = pcre2_match_data_create(m_fil_inst.ovector_size(), NULL);
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
bool RegexHintFSession::routeQuery(GWBUF* queue)
{
    char* sql = NULL;
    int sql_len = 0;

    if (modutil_is_SQL(queue) && m_active)
    {
        if (modutil_extract_SQL(queue, &sql, &sql_len))
        {
            const RegexToServers* reg_serv = find_servers(sql, sql_len);
            if (reg_serv)
            {
                /* Add the servers in the list to the buffer routing hints */
                for (const auto& target : reg_serv->m_targets)
                {
                    queue->hint = hint_create_route(queue->hint, reg_serv->m_htype, target.c_str());
                }
                m_n_diverted++;
                m_fil_inst.m_total_diverted++;
            }
            else
            {
                m_n_undiverted++;
                m_fil_inst.m_total_undiverted++;
            }
        }
    }
    return FilterSession::routeQuery(queue);
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param session   The client session to attach to
 * @return a new filter session
 */
mxs::FilterSession* RegexHintFilter::newSession(MXS_SESSION* session, SERVICE* service)
{
    bool session_active = true;
    bool ip_found = false;
    auto& sett = m_settings;
    /* Check client IP against 'source' host option */
    auto& remote = session->client_remote();
    auto& remote_addr = session->client_connection()->dcb()->ip();
    if (!m_sources.empty())
    {
        ip_found = check_source_host(remote.c_str(), &remote_addr);
        session_active = ip_found;
    }
    /* Don't check hostnames if ip is already found */
    if (!m_hostnames.empty() && !ip_found)
    {
        session_active = check_source_hostnames(&remote_addr);
    }

    /* Check client user against 'user' option */
    if (!sett.m_user.empty() && (sett.m_user != session->user()))
    {
        session_active = false;
    }
    return new RegexHintFSession(session, service, *this, session_active);
}

/**
 * Find the first server list with a matching regular expression.
 *
 * @param sql   SQL-query string, not null-terminated
 * @paran sql_len   length of SQL-query
 * @return A set of servers from the main mapping container
 */
const RegexToServers* RegexHintFSession::find_servers(char* sql, int sql_len)
{
    /* Go through the regex array and find a match. */
    for (auto& regex_map : m_fil_inst.mapping())
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
RegexHintFilter* RegexHintFilter::create(const char* name)
{
    return new RegexHintFilter(name);
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

    json_object_set_new(rval, "queries_diverted", json_integer(m_total_diverted));
    json_object_set_new(rval, "queries_undiverted", json_integer(m_total_undiverted));

    if (m_mapping.size() > 0)
    {
        json_t* arr = json_array();

        for (const auto& regex_map : m_mapping)
        {
            json_t* obj = json_object();
            json_t* targets = json_array();

            for (const auto& target : regex_map.m_targets)
            {
                json_array_append_new(targets, json_string(target.c_str()));
            }

            json_object_set_new(obj, "match", json_string(regex_map.m_match.c_str()));
            json_object_set_new(obj, "targets", targets);
        }

        json_object_set_new(rval, "mappings", arr);
    }

    if (!m_sources.empty())
    {
        json_t* arr = json_array();

        for (const auto& source : m_sources)
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
        /* The string contains a server list. Check that all names are valid. */
        auto servers = SERVER::server_find_by_unique_names(targets_array);
        for (size_t i = 0; i < servers.size(); i++)
        {
            if (servers[i] == nullptr)
            {
                error = true;
                MXS_ERROR("'%s' is not a valid server name.", targets_array[i].c_str());
            }
        }

        if (!error)
        {
            for (const auto& elem : targets_array)
            {
                m_targets.push_back(elem);
            }
        }
    }
    else if (targets_array.size() == 1)
    {
        /* The string is either a server name or a special reserved id */
        auto& only_elem = targets_array[0];
        if (SERVER::find_by_unique_name(only_elem))
        {
            m_targets.push_back(only_elem);
        }
        else if (only_elem == "->master")
        {
            m_targets.push_back(only_elem);
            m_htype = HINT_ROUTE_TO_MASTER;
        }
        else if (only_elem == "->slave")
        {
            m_targets.push_back(only_elem);
            m_htype = HINT_ROUTE_TO_SLAVE;
        }
        else if (only_elem == "->all")
        {
            m_targets.push_back(only_elem);
            m_htype = HINT_ROUTE_TO_ALL;
        }
        else
        {
            error = true;
        }
    }
    else
    {
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

bool RegexHintFilter::regex_compile_and_add(int pcre_ops, bool legacy_mode, const std::string& match,
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
        if (pcre2_jit_compile(regex, PCRE2_JIT_COMPLETE) < 0)
        {
            MXS_NOTICE("PCRE2 JIT compilation of pattern '%s' failed, falling back to normal compilation.",
                       match.c_str());
        }

        RegexToServers mapping_elem(match, regex);
        if (mapping_elem.add_targets(target, legacy_mode))
        {
            m_mapping.push_back(std::move(mapping_elem));

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
                if (required_ovec_size > m_ovector_size)
                {
                    m_ovector_size = required_ovec_size;
                }
            }
        }
        else
        {
            // The targets string didn't seem to contain a valid routing target.
            MXS_ERROR("Could not parse a routing target from '%s'.", target.c_str());
            success = false;
        }
    }
    else
    {
        MXS_ERROR("Invalid PCRE2 regular expression '%s' (position '%zu').",
                  match.c_str(), error_offset);
        MXS_PCRE2_PRINT_ERROR(errorcode);
        success = false;
    }
    return success;
}

/**
 * Read all indexed regexes from the supplied configuration, compile them and form the mapping
 */
void RegexHintFilter::form_regex_server_mapping(int pcre_ops)
{
    auto& regex_values = m_settings.m_match_targets;
    bool error = false;

    /* The config parameters can be in any order and may be skipping numbers. Go through all params and
     * save found ones to array. */
    std::vector<Settings::MatchAndTarget> found_pairs;

    const char missing_setting[] = "'%s' does not have a matching '%s'.";
    for (size_t i = 0; i < RegexHintFilter::Settings::n_regex_max; i++)
    {
        auto& param_definition = s_match_target_specs[i];
        auto& param_name_match = param_definition.match->name();
        auto& param_name_target = param_definition.target->name();

        auto& param_val = regex_values[i];

        /* Check that both the matchXY and targetXY settings are found. */
        bool match_exists = !param_val.match.empty();
        bool target_exists = !param_val.target.empty();

        if (match_exists && target_exists)
        {
            found_pairs.push_back(param_val);
        }
        else if (match_exists)
        {
            MXB_ERROR(missing_setting, param_name_match.c_str(), param_name_target.c_str());
            error = true;
        }
        else if (target_exists)
        {
            MXB_ERROR(missing_setting, param_name_target.c_str(), param_name_match.c_str());
            error = true;
        }
    }

    m_mapping.clear();
    if (!error)
    {
        for (const auto& elem : found_pairs)
        {
            if (!regex_compile_and_add(pcre_ops, false, elem.match, elem.target))
            {
                error = true;
            }
        }
    }

    if (error)
    {
        m_mapping.clear();
    }
}

/**
 * Check whether the client IP matches the configured 'source' host,
 * which can have up to three % wildcards.
 *
 * @param remote      The clientIP
 * @param ipv4        The client socket address struct
 * @return            true for match, false otherwise
 */
bool RegexHintFilter::check_source_host(const char* remote, const struct sockaddr_storage* ip)
{
    bool rval = false;
    struct sockaddr_storage addr;
    memcpy(&addr, ip, sizeof(addr));

    for (const auto& source : m_sources)
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
            MXS_INFO("Client IP %s matches host source %s%s",
                     remote,
                     source.m_netmask < 128 ? "with wildcards " : "",
                     source.m_address.c_str());
            return rval;
        }
    }

    return rval;
}

bool RegexHintFilter::check_source_hostnames(const struct sockaddr_storage* ip)
{
    struct sockaddr_storage addr;
    memcpy(&addr, ip, sizeof(addr));
    char hbuf[NI_MAXHOST];

    int rc = getnameinfo((struct sockaddr*)&addr, sizeof(addr), hbuf, sizeof(hbuf), NULL, 0, NI_NAMEREQD);

    if (rc != 0)
    {
        MXS_INFO("Failed to resolve hostname due to %s", gai_strerror(rc));
        return false;
    }

    for (const auto& host : m_hostnames)
    {
        if (strcmp(hbuf, host.c_str()) == 0)
        {
            MXS_INFO("Client hostname %s matches host source %s", hbuf, host.c_str());
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
bool RegexHintFilter::add_source_address(const std::string& input_host)
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
        MXS_INFO("Input %s is valid with netmask %d", address.c_str(), netmask);
        freeaddrinfo(ai);
    }
    else
    {
        return false;
    }
    m_sources.emplace_back(address, ipv6, netmask);
    return true;
}

void RegexHintFilter::set_source_addresses(const std::string& host_names)
{
    for (const auto& host : mxs::strtok(host_names, ","))
    {
        std::string trimmed_host = host;
        mxb::trim(trimmed_host);

        if (!add_source_address(trimmed_host))
        {
            MXS_INFO("The given 'source' parameter '%s' is not a valid IP address. Adding it as hostname.",
                     trimmed_host.c_str());
            m_hostnames.emplace_back(trimmed_host);
        }
    }
}

int RegexHintFilter::ovector_size() const
{
    return m_ovector_size;
}

bool RegexHintFilter::post_configure()
{
    const char MATCH_STR[] = "match";
    const char SERVER_STR[] = "server";
    const char TARGET_STR[] = "target";

    auto& sett = m_settings;

    if (!sett.m_source.empty())
    {
        set_source_addresses(sett.m_source);
    }

    int pcre_ops = sett.m_regex_options;

    const bool legacy_mode = (!sett.m_match.empty() || !sett.m_server.empty());
    bool error = false;
    if (legacy_mode && (sett.m_match.empty() || sett.m_server.empty()))
    {
        MXS_ERROR("Only one of '%s' and '%s' is set. If using legacy mode, set both."
                  "If using indexed parameters, set neither and use '%s01' and '%s01' etc.",
                  MATCH_STR, SERVER_STR, MATCH_STR, TARGET_STR);
        error = true;
    }

    /* Try to form the mapping with indexed parameter names. */
    form_regex_server_mapping(pcre_ops);

    if (!legacy_mode && m_mapping.empty())
    {
        MXS_ERROR("Could not parse any indexed '%s'-'%s' pairs.", MATCH_STR, TARGET_STR);
        error = true;
    }
    else if (legacy_mode && !m_mapping.empty())
    {
        MXS_ERROR("Found both legacy parameters and indexed parameters. Use only one type of parameters.");
        error = true;
    }
    else if (legacy_mode && m_mapping.empty())
    {
        MXS_WARNING("Use of legacy parameters 'match' and 'server' is deprecated.");
        /* Using legacy mode and no indexed parameters found. Add the legacy parameters
         * to the mapping. */
        if (!regex_compile_and_add(pcre_ops, true, sett.m_match, sett.m_server))
        {
            error = true;
        }
    }
    return !error;
}

MappingVector& RegexHintFilter::mapping()
{
    return m_mapping;
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
        MXS_MODULE_NAME,
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
        {
        },
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

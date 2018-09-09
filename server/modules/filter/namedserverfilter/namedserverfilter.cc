/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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

#include <maxscale/alloc.h>
#include <maxscale/hint.h>
#include <maxscale/log.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/server.h>
#include <maxscale/utils.h>

static void generate_param_names(int pairs);

/* These arrays contain the allowed indexed config parameter names. match01,
 * target01, match02, target02 ... */
static StringVector param_names_match_indexed;
static StringVector param_names_target_indexed;

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS                                    },
    {"case",       0                                                 },
    {"extended",   PCRE2_EXTENDED                                    }, // Ignore white space and # comments
    {NULL}
};

static const char MATCH_STR[] = "match";
static const char SERVER_STR[] = "server";
static const char TARGET_STR[] = "target";

RegexHintFilter::RegexHintFilter(const std::string& user,
                                 const SourceHostVector& addresses,
                                 const StringVector& hostnames,
                                 const MappingVector& mapping,
                                 int ovector_size)
    : m_user(user)
    , m_sources(addresses)
    , m_hostnames(hostnames)
    , m_mapping(mapping)
    , m_ovector_size(ovector_size)
    , m_total_diverted(0)
    , m_total_undiverted(0)
{
}

RegexHintFilter::~RegexHintFilter()
{
    for (const auto& regex : m_mapping)
    {
        pcre2_code_free(regex.m_regex);
    }
}

RegexHintFSession::RegexHintFSession(MXS_SESSION* session,
                                     RegexHintFilter& fil_inst,
                                     bool active,
                                     pcre2_match_data* md)
    : maxscale::FilterSession::FilterSession(session)
    , m_fil_inst(fil_inst)
    , m_n_diverted(0)
    , m_n_undiverted(0)
    , m_active(active)
    , m_match_data(md)
{
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
 * @return 1 on success, 0 on failure
 */
int RegexHintFSession::routeQuery(GWBUF* queue)
{
    char* sql = NULL;
    int sql_len = 0;

    if (modutil_is_SQL(queue) && m_active)
    {
        if (modutil_extract_SQL(queue, &sql, &sql_len))
        {
            const RegexToServers* reg_serv
                = m_fil_inst.find_servers(sql, sql_len, m_match_data);

            if (reg_serv)
            {
                /* Add the servers in the list to the buffer routing hints */
                for (const auto& target : reg_serv->m_targets)
                {
                    queue->hint
                        = hint_create_route(queue->hint, reg_serv->m_htype, target.c_str());
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
    return m_down.routeQuery(queue);
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param session   The client session to attach to
 * @return a new filter session
 */
RegexHintFSession* RegexHintFilter::newSession(MXS_SESSION* session)
{
    const char* remote = NULL;
    const char* user = NULL;

    pcre2_match_data* md = pcre2_match_data_create(m_ovector_size, NULL);
    bool session_active = true;
    bool ip_found = false;

    /* Check client IP against 'source' host option */
    if ((remote = session_get_remote(session)) != NULL)
    {
        if (m_sources.size() > 0)
        {
            ip_found = check_source_host(remote, &(session->client_dcb->ip));
            session_active = ip_found;
        }
        /* Don't check hostnames if ip is already found */
        if (m_hostnames.size() > 0 && ip_found == false)
        {
            session_active = check_source_hostnames(remote, &(session->client_dcb->ip));
        }
    }
    /* Check client user against 'user' option */
    if (m_user.length() > 0
        && ((user = session_get_user(session)) != NULL)
        && (user != m_user))
    {
        session_active = false;
    }
    return new RegexHintFSession(session, *this, session_active, md);
}

/**
 * Find the first server list with a matching regular expression.
 *
 * @param sql   SQL-query string, not null-terminated
 * @paran sql_len   length of SQL-query
 * @param match_data    result container, from filter session
 * @return a set of servers from the main mapping container
 */
const RegexToServers* RegexHintFilter::find_servers(char* sql, int sql_len, pcre2_match_data* match_data)
{
    /* Go through the regex array and find a match. */
    for (auto& regex_map : m_mapping)
    {
        pcre2_code* regex = regex_map.m_regex;
        int result = pcre2_match(regex,
                                 (PCRE2_SPTR)sql,
                                 sql_len,
                                 0,
                                 0,
                                 match_data,
                                 NULL);
        if (result >= 0)
        {
            /* Have a match. No need to check if the regex matches the complete
             * query, since the user can form the regex to enforce this. */
            return &(regex_map);
        }
        else if (result != PCRE2_ERROR_NOMATCH)
        {
            /* Error during matching */
            if (!regex_map.m_error_printed)
            {
                MXS_PCRE2_PRINT_ERROR(result);
                regex_map.m_error_printed = true;
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
uint64_t RegexHintFilter::getCapabilities()
{
    return RCAP_TYPE_NONE;
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
RegexHintFilter* RegexHintFilter::create(const char* name, MXS_CONFIG_PARAMETER* params)
{
    bool error = false;
    SourceHostVector source_addresses;
    StringVector source_hostnames;

    const char* source = config_get_string(params, "source");

    if (*source)
    {
        set_source_addresses(source, source_addresses, source_hostnames);
    }

    int pcre_ops = config_get_enum(params, "options", option_values);

    std::string match_val_legacy(config_get_string(params, MATCH_STR));
    std::string server_val_legacy(config_get_string(params, SERVER_STR));
    const bool legacy_mode = (match_val_legacy.length() || server_val_legacy.length());

    if (legacy_mode && (!match_val_legacy.length() || !server_val_legacy.length()))
    {
        MXS_ERROR("Only one of '%s' and '%s' is set. If using legacy mode, set both."
                  "If using indexed parameters, set neither and use '%s01' and '%s01' etc.",
                  MATCH_STR,
                  SERVER_STR,
                  MATCH_STR,
                  TARGET_STR);
        error = true;
    }

    MappingVector mapping;
    uint32_t max_capcount;
    /* Try to form the mapping with indexed parameter names */
    form_regex_server_mapping(params, pcre_ops, &mapping, &max_capcount);

    if (!legacy_mode && !mapping.size())
    {
        MXS_ERROR("Could not parse any indexed '%s'-'%s' pairs.", MATCH_STR, TARGET_STR);
        error = true;
    }
    else if (legacy_mode && mapping.size())
    {
        MXS_ERROR("Found both legacy parameters and indexed parameters. Use only "
                  "one type of parameters.");
        error = true;
    }
    else if (legacy_mode && !mapping.size())
    {
        MXS_WARNING("Use of legacy parameters 'match' and 'server' is deprecated.");
        /* Using legacy mode and no indexed parameters found. Add the legacy parameters
         * to the mapping. */
        if (!regex_compile_and_add(pcre_ops,
                                   true,
                                   match_val_legacy,
                                   server_val_legacy,
                                   &mapping,
                                   &max_capcount))
        {
            error = true;
        }
    }

    if (error)
    {
        return NULL;
    }
    else
    {
        RegexHintFilter* instance = NULL;
        std::string user(config_get_string(params, "user"));
        MXS_EXCEPTION_GUARD(instance
                                = new RegexHintFilter(user,
                                                      source_addresses,
                                                      source_hostnames,
                                                      mapping,
                                                      max_capcount + 1));
        return instance;
    }
}

/**
 * Diagnostics routine
 *
 * Print diagnostics on the filter instance as a whole + session-specific info.
 *
 * @param   dcb     The DCB for diagnostic output
 */
void RegexHintFSession::diagnostics(DCB* dcb)
{

    m_fil_inst.diagnostics(dcb);    /* Print overall diagnostics */
    dcb_printf(dcb,
               "\t\tNo. of queries diverted by filter (session): %d\n",
               m_n_diverted);
    dcb_printf(dcb,
               "\t\tNo. of queries not diverted by filter (session):     %d\n",
               m_n_undiverted);
}

/**
 * Diagnostics routine
 *
 * Print diagnostics on the filter instance as a whole + session-specific info.
 *
 * @param   dcb     The DCB for diagnostic output
 */
json_t* RegexHintFSession::diagnostics_json() const
{

    json_t* rval = m_fil_inst.diagnostics_json();   /* Print overall diagnostics */

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
void RegexHintFilter::diagnostics(DCB* dcb)
{
    if (m_mapping.size() > 0)
    {
        dcb_printf(dcb, "\t\tMatches and routes:\n");
    }

    for (const auto& regex_map : m_mapping)
    {
        dcb_printf(dcb,
                   "\t\t\t/%s/ -> ",
                   regex_map.m_match.c_str());

        for (const auto& target : regex_map.m_targets)
        {
            dcb_printf(dcb, ", %s", target.c_str());
        }
        dcb_printf(dcb, "\n");
    }
    dcb_printf(dcb,
               "\t\tTotal no. of queries diverted by filter (approx.):     %d\n",
               m_total_diverted);
    dcb_printf(dcb,
               "\t\tTotal no. of queries not diverted by filter (approx.): %d\n",
               m_total_undiverted);

    for (const auto& source : m_sources)
    {
        dcb_printf(dcb,
                   "\t\tReplacement limited to connections from     %s\n",
                   source.m_address.c_str());
    }

    if (m_user.length())
    {
        dcb_printf(dcb,
                   "\t\tReplacement limit to user           %s\n",
                   m_user.c_str());
    }
}

/**
 * Diagnostics routine
 *
 * Print diagnostics on the filter instance as a whole.
 *
 * @param   dcb     The DCB for diagnostic output
 */
json_t* RegexHintFilter::diagnostics_json() const
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

    if (m_user.length())
    {
        json_object_set_new(rval, "user", json_string(m_user.c_str()));
    }

    return rval;
}

/**
 * Parse the server list and add the contained servers to the struct's internal
 * list. Server names are verified to be valid servers.
 *
 * @param server_names The list of servers as read from the config file
 * @return How many were found
 */
int RegexToServers::add_servers(const std::string& server_names, bool legacy_mode)
{
    if (legacy_mode)
    {
        /* Should have just one server name, already known to be valid */
        m_targets.push_back(server_names);
        return 1;
    }

    /* Have to parse the server list here instead of in config loader, since the list
     * may contain special placeholder strings.
     */
    bool error = false;
    char** names_arr = NULL;
    const int n_names = config_parse_server_list(server_names.c_str(), &names_arr);

    if (n_names > 1)
    {
        /* The string contains a server list, all must be valid servers */
        SERVER** servers;
        int found = server_find_by_unique_names(names_arr, n_names, &servers);

        if (found != n_names)
        {
            error = true;

            for (int i = 0; i < n_names; i++)
            {
                /* servers is valid only if found > 0 */
                if (!found || !servers[i])
                {
                    MXS_ERROR("'%s' is not a valid server name.", names_arr[i]);
                }
            }
        }

        if (found)
        {
            MXS_FREE(servers);
        }

        if (!error)
        {
            for (int i = 0; i < n_names; i++)
            {
                m_targets.push_back(names_arr[i]);
            }
        }
    }
    else if (n_names == 1)
    {
        /* The string is either a server name or a special reserved id */
        if (server_find_by_unique_name(names_arr[0]))
        {
            m_targets.push_back(names_arr[0]);
        }
        else if (strcmp(names_arr[0], "->master") == 0)
        {
            m_targets.push_back(names_arr[0]);
            m_htype = HINT_ROUTE_TO_MASTER;
        }
        else if (strcmp(names_arr[0], "->slave") == 0)
        {
            m_targets.push_back(names_arr[0]);
            m_htype = HINT_ROUTE_TO_SLAVE;
        }
        else if (strcmp(names_arr[0], "->all") == 0)
        {
            m_targets.push_back(names_arr[0]);
            m_htype = HINT_ROUTE_TO_ALL;
        }
        else
        {
            error = true;
        }
    }
    else
    {
        error = true;
    }

    for (int i = 0; i < n_names; i++)
    {
        MXS_FREE(names_arr[i]);
    }
    MXS_FREE(names_arr);
    return error ? 0 : n_names;
}

bool RegexHintFilter::regex_compile_and_add(int pcre_ops,
                                            bool legacy_mode,
                                            const std::string& match,
                                            const std::string& servers,
                                            MappingVector* mapping,
                                            uint32_t* max_capcount)
{
    bool success = true;
    int errorcode = -1;
    PCRE2_SIZE error_offset = -1;
    pcre2_code* regex
        = pcre2_compile((PCRE2_SPTR) match.c_str(),
                        match.length(),
                        pcre_ops,
                        &errorcode,
                        &error_offset,
                        NULL);

    if (regex)
    {
        // Try to compile even further for faster matching
        if (pcre2_jit_compile(regex, PCRE2_JIT_COMPLETE) < 0)
        {
            MXS_NOTICE("PCRE2 JIT compilation of pattern '%s' failed, "
                       "falling back to normal compilation.",
                       match.c_str());
        }

        RegexToServers regex_ser(match, regex);

        if (regex_ser.add_servers(servers, legacy_mode) == 0)
        {
            // The servers string didn't seem to contain any servers
            MXS_ERROR("Could not parse servers from string '%s'.", servers.c_str());
            success = false;
        }
        mapping->push_back(regex_ser);

        /* Check what is the required match_data size for this pattern. The
         * largest value is used to form the match data.
         */
        uint32_t capcount = 0;
        int ret_info = pcre2_pattern_info(regex, PCRE2_INFO_CAPTURECOUNT, &capcount);

        if (ret_info != 0)
        {
            MXS_PCRE2_PRINT_ERROR(ret_info);
            success = false;
        }
        else
        {
            if (capcount > *max_capcount)
            {
                *max_capcount = capcount;
            }
        }
    }
    else
    {
        MXS_ERROR("Invalid PCRE2 regular expression '%s' (position '%zu').",
                  match.c_str(),
                  error_offset);
        MXS_PCRE2_PRINT_ERROR(errorcode);
        success = false;
    }
    return success;
}

/**
 * Read all indexed regexes from the supplied configuration, compile them and form the mapping
 *
 * @param params config parameters
 * @param pcre_ops options for pcre2_compile
 * @param mapping An array of regex->serverlist mappings for filling in. Is cleared on error.
 * @param max_capcount_out The maximum detected pcre2 capture count is written here.
 */
void RegexHintFilter::form_regex_server_mapping(MXS_CONFIG_PARAMETER* params,
                                                int pcre_ops,
                                                MappingVector* mapping,
                                                uint32_t* max_capcount_out)
{
    mxb_assert(param_names_match_indexed.size() == param_names_target_indexed.size());
    bool error = false;
    uint32_t max_capcount = 0;
    *max_capcount_out = 0;
    /* The config parameters can be in any order and may be skipping numbers.
     * Must just search for every possibility. Quite inefficient, but this is
     * only done once. */
    for (unsigned int i = 0; i < param_names_match_indexed.size(); i++)
    {
        const char* param_name_match = param_names_match_indexed[i].c_str();
        const char* param_name_target = param_names_target_indexed[i].c_str();
        std::string match(config_get_string(params, param_name_match));
        std::string target(config_get_string(params, param_name_target));

        /* Check that both the regex and server config parameters are found */
        if (match.length() < 1 || target.length() < 1)
        {
            if (match.length() > 0)
            {
                MXS_ERROR("No server defined for regex setting '%s'.", param_name_match);
                error = true;
            }
            else if (target.length() > 0)
            {
                MXS_ERROR("No regex defined for server setting '%s'.", param_name_target);
                error = true;
            }
            continue;
        }

        if (!regex_compile_and_add(pcre_ops, false, match, target, mapping, &max_capcount))
        {
            error = true;
        }
    }

    if (error)
    {
        for (unsigned int i = 0; i < mapping->size(); i++)
        {
            pcre2_code_free(mapping->at(i).m_regex);
        }
        mapping->clear();
    }
    else
    {
        *max_capcount_out = max_capcount;
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

bool RegexHintFilter::check_source_hostnames(const char* remote, const struct sockaddr_storage* ip)
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
            MXS_INFO("Client hostname %s matches host source %s",
                     hbuf,
                     host.c_str());
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
    if (*host == '%'
        || *host == '.'
        || strlen(host) > INET_ADDRSTRLEN)
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
bool RegexHintFilter::add_source_address(const char* input_host, SourceHostVector& source_hosts)
{
    std::string address(input_host);
    struct sockaddr_in6 ipv6 = {};
    int netmask = 128;
    std::string format_host = address;
    /* If no wildcards, leave netmask to 128 and return */
    if (strchr(input_host, '%') && validate_ipv4_address(input_host))
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
    source_hosts.emplace_back(address, ipv6, netmask);
    return true;
}

void RegexHintFilter::set_source_addresses(const std::string& input_host_names,
                                           SourceHostVector&  source_hosts,
                                           StringVector& hostnames)
{
    std::string host_names(input_host_names);

    for (auto host : mxs::strtok(host_names, ","))
    {
        char* trimmed_host = trim((char*)host.c_str());

        if (!add_source_address(trimmed_host, source_hosts))
        {
            MXS_INFO("The given 'source' parameter '%s' is not a valid IP address. "
                     "adding it as hostname.",
                     trimmed_host);
            hostnames.emplace_back(trimmed_host);
        }
    }
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
    static MXS_FILTER_OBJECT MyObject = RegexHintFilter::s_object;

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        "A routing hint filter that uses regular expressions to direct queries",
        "V1.1.0",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &MyObject,
        NULL,                                                                   /* Process init. */
        NULL,                                                                   /* Process finish. */
        NULL,                                                                   /* Thread init. */
        NULL,                                                                   /* Thread finish. */
        {
            {"source",
             MXS_MODULE_PARAM_STRING                                                                                                                         },
            {"user",
             MXS_MODULE_PARAM_STRING                                                                                                                                                                                  },
            {MATCH_STR,
             MXS_MODULE_PARAM_STRING                                                                                                                                                                                                                                          },
            {SERVER_STR,
             MXS_MODULE_PARAM_SERVER                                                                                                                                                                                                                                          },
            {
                "options",
                MXS_MODULE_PARAM_ENUM,
                "ignorecase",
                MXS_MODULE_OPT_NONE,
                option_values
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    /* This module takes parameters of the form match, match01, match02, ... matchN
     * and server, server01, server02, ... serverN. The total number of module
     * parameters is limited, so let's limit the number of matches and servers.
     * First, loop over the already defined parameters... */
    int params_counter = 0;

    while (info.parameters[params_counter].name != MXS_END_MODULE_PARAMS)
    {
        params_counter++;
    }

    /* Calculate how many pairs can be added. 100 is max (to keep the postfix
     * number within two decimals). */
    const int max_pairs = 100;
    int match_server_pairs = ((MXS_MODULE_PARAM_MAX - params_counter) / 2);

    if (match_server_pairs > max_pairs)
    {
        match_server_pairs = max_pairs;
    }
    mxb_assert(match_server_pairs >= 25);   // If this limit is modified, update documentation.
    /* Create parameter pair names */
    generate_param_names(match_server_pairs);

    /* Now make the actual parameters for the module struct */
    MXS_MODULE_PARAM new_param_match = {NULL, MXS_MODULE_PARAM_STRING, NULL};
    /* Cannot use SERVERLIST in the target, since it may contain MASTER, SLAVE. */
    MXS_MODULE_PARAM new_param_target = {NULL, MXS_MODULE_PARAM_STRING, NULL};

    for (unsigned int i = 0; i < param_names_match_indexed.size(); ++i)
    {
        new_param_match.name = param_names_match_indexed.at(i).c_str();
        info.parameters[params_counter] = new_param_match;
        params_counter++;
        new_param_target.name = param_names_target_indexed.at(i).c_str();
        info.parameters[params_counter] = new_param_target;
        params_counter++;
    }
    info.parameters[params_counter].name = MXS_END_MODULE_PARAMS;

    return &info;
}

/*
 * Generate N pairs of parameter names of form matchXX and targetXX and add them
 * to the global arrays.
 *
 * @param pairs The number of parameter pairs to generate
 */
static void generate_param_names(int pairs)
{
    const int namelen_match = sizeof(MATCH_STR) + 2;
    const int namelen_server = sizeof(TARGET_STR) + 2;

    char name_match[namelen_match];
    char name_server[namelen_server];

    const char FORMAT[] = "%s%02d";

    for (int counter = 1; counter <= pairs; ++counter)
    {
        MXB_AT_DEBUG(int rval = ) snprintf(name_match, namelen_match, FORMAT, MATCH_STR, counter);
        mxb_assert(rval == namelen_match - 1);
        MXB_AT_DEBUG(rval = ) snprintf(name_server, namelen_server, FORMAT, TARGET_STR, counter);
        mxb_assert(rval == namelen_server - 1);

        // Have both names, add them to the global vectors
        param_names_match_indexed.push_back(name_match);
        param_names_target_indexed.push_back(name_server);
    }
}

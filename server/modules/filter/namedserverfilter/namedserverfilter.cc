/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
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
#include <maxscale/log_manager.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/utils.h>

using std::string;

static void generate_param_names(int pairs);

/* These arrays contain the possible config parameter names. */
static StringArray param_names_match;
static StringArray param_names_server;

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case", 0},
    {"extended", PCRE2_EXTENDED}, // Ignore white space and # comments
    {NULL}
};

RegexHintFilter::RegexHintFilter(string user, SourceHost* source,
                                 const MappingArray& mapping, int ovector_size)
    :   m_user(user),
        m_source(source),
        m_mapping(mapping),
        m_ovector_size(ovector_size),
        m_total_diverted(0),
        m_total_undiverted(0)
{}

RegexHintFilter::~RegexHintFilter()
{
    delete m_source;
    pcre2_code_free(m_regex);
    if (m_source)
    {
        MXS_FREE(m_source->address);
    }
    MXS_FREE(m_source);
    for (unsigned int i = 0; i < m_mapping.size(); i++)
    {
        pcre2_code_free(m_mapping.at(i).m_regex);
    }
}

RegexHintFSession::RegexHintFSession(MXS_SESSION* session,
                                     RegexHintFilter& fil_inst,
                                     bool active, pcre2_match_data* md)
    : maxscale::FilterSession::FilterSession(session),
      m_fil_inst(fil_inst),
      m_n_diverted(0),
      m_n_undiverted(0),
      m_active(active),
      m_match_data(md)
{}

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
            const RegexToServers* reg_serv =
                m_fil_inst.find_servers(m_match_data, sql, sql_len);

            if (reg_serv)
            {
                /* Add the servers in the list to the buffer routing hints */
                for (unsigned int i = 0; i < reg_serv->m_servers.size(); i++)
                {
                    queue->hint =
                        hint_create_route(queue->hint, HINT_ROUTE_TO_NAMED_SERVER,
                                          ((reg_serv->m_servers)[i]).c_str());
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

    /* Check client IP against 'source' host option */
    if (m_source && m_source->m_address &&
        (remote = session_get_remote(session)) != NULL)
    {
        session_active =
            check_source_host(remote, &(session->client_dcb->ipv4));
    }

    /* Check client user against 'user' option */
    if (m_user.length() > 0 &&
        ((user = session_get_user(session)) != NULL) &&
        (user != m_user))
    {
        session_active = false;
    }
    return new RegexHintFSession(session, *this, session_active, md);
}

/**
 * Find the first server list with a matching regural expression.
 *
 * @param match_data    result container, from filter session
 * @param sql   SQL-query string, not null-terminated
 * @paran sql_len length of SQL-query
 * @return a set of servers from the main mapping container
 */
const RegexToServers*
RegexHintFilter::find_servers(pcre2_match_data* match_data, char* sql, int sql_len)
{
    /* Go through the regex array and find a match. */
    for (unsigned int i = 0; i < m_mapping.size(); i++)
    {
        pcre2_code* regex = m_mapping[i].m_regex;
        int result = pcre2_match(regex, (PCRE2_SPTR)sql, sql_len, 0, 0,
                                 match_data, NULL);
        if (result >= 0)
        {
            /* Have a match. No need to check if the regex matches the complete
             * query, since the user can form the regex to enforce this. */
            return &(m_mapping[i]);
        }
        else if (result != PCRE2_ERROR_NOMATCH)
        {
            /* Error during matching */
            if (!m_mapping[i].m_error_printed)
            {
                MXS_PCRE2_PRINT_ERROR(result);
                m_mapping[i].m_error_printed = true;
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
    return RCAP_TYPE_CONTIGUOUS_INPUT;
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
RegexHintFilter*
RegexHintFilter::create(const char* name, char** options, MXS_CONFIG_PARAMETER* params)
{
    bool error = false;
    SourceHost* source_host = NULL;
    /* The cfg_param cannot be changed to string because set_source_address doesn't
       copy the contents. This inefficient as the config string searching */
    const char* source = config_get_string(params, "source");
    if (*source)
    {
        source_host = set_source_address(source);
        if (!source_host)
        {
            MXS_ERROR("Failure setting 'source' from %s", source);
            error = true;
        }
    }

    int pcre_ops = config_get_enum(params, "options", option_values);
    MappingArray mapping;
    uint32_t max_capcount;
    form_regex_server_mapping(params, pcre_ops, &mapping, &max_capcount);

    if (!mapping.size() || error)
    {
        delete source_host;
        return NULL;
    }
    else
    {
        RegexHintFilter* instance = NULL;
        string user(config_get_string(params, "user"));
        MXS_EXCEPTION_GUARD(instance =
                                new RegexHintFilter(user, source_host, mapping, max_capcount + 1));
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

    m_fil_inst.diagnostics(dcb); /* Print overall diagnostics */
    dcb_printf(dcb, "\t\tNo. of queries diverted by filter (session): %d\n",
               m_n_diverted);
    dcb_printf(dcb, "\t\tNo. of queries not diverted by filter (session):     %d\n",
               m_n_undiverted);
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
    for (unsigned int i = 0; i < m_mapping.size(); i++)
    {
        dcb_printf(dcb, "\t\t\t/%s/ -> ",
                   m_mapping[i].m_match.c_str());
        dcb_printf(dcb, "%s", m_mapping[i].m_servers[0].c_str());
        for (unsigned int j = 1; j < m_mapping[i].m_servers.size(); j++)
        {
            dcb_printf(dcb, ", %s", m_mapping[i].m_servers[j].c_str());
        }
        dcb_printf(dcb, "\n");
    }
    dcb_printf(dcb, "\t\tTotal no. of queries diverted by filter (approx.):     %d\n",
               m_total_diverted);
    dcb_printf(dcb, "\t\tTotal no. of queries not diverted by filter (approx.): %d\n",
               m_total_undiverted);

    if (m_source)
    {
        dcb_printf(dcb,
                   "\t\tReplacement limited to connections from     %s\n",
                   m_source->m_address);
    }
    if (m_user.length())
    {
        dcb_printf(dcb,
                   "\t\tReplacement limit to user           %s\n",
                   m_user.c_str());
    }
}

/**
 * Parse the server list and add the contained servers to the struct's internal
 * list. Server names are not verified to be valid servers.
 *
 * @param server_names The list of servers as read from the config file
 * @return How many were found
 */
int RegexToServers::add_servers(string server_names)
{
    /* Parse the list, server names separated by ','. Do as in config.c :
     * configure_new_service() to stay compatible. We cannot check here
     * (at least not easily) if the server is named correctly, since the
     * filter doesn't even know its service. */
    char servers[server_names.length() + 1];
    strcpy(servers, server_names.c_str());
    int found = 0;
    char* lasts;
    char* s = strtok_r(servers, ",", &lasts);
    while (s)
    {
        m_servers.push_back(s);
        found++;
        s = strtok_r(NULL, ",", &lasts);
    }
    return found;
}

/**
 * Read all regexes from the supplied configuration, compile them and form the mapping
 *
 * @param mapping An array of regex->serverlist mappings for filling in. Is cleared on error.
 * @param pcre_ops options for pcre2_compile
 * @param params config parameters
 */
void RegexHintFilter::form_regex_server_mapping(MXS_CONFIG_PARAMETER* params, int pcre_ops,
                                                MappingArray* mapping, uint32_t* max_capcount_out)
{
    ss_dassert(param_names_match.size() == param_names_server.size());
    bool error = false;
    uint32_t max_capcount = 0;
    *max_capcount_out = 0;
    /* The config parameters can be in any order and may be skipping numbers.
     * Must just search for every possibility. Quite inefficient, but this is
     * only done once. */
    for (unsigned int i = 0; i < param_names_match.size(); i++)
    {
        const char* match_param_name = param_names_match[i].c_str();
        const char* server_param_name = param_names_server[i].c_str();
        string match(config_get_string(params, match_param_name));
        string servers(config_get_string(params, server_param_name));

        /* Check that both the regex and server config parameters are found */
        if (match.length() < 1 || servers.length() < 1)
        {
            if (match.length() > 0)
            {
                MXS_ERROR("No server defined for regex setting '%s'.", match_param_name);
                error = true;
            }
            else if (servers.length() > 0)
            {
                MXS_ERROR("No regex defined for server setting '%s'.", server_param_name);
                error = true;
            }
            continue;
        }

        int errorcode = -1;
        PCRE2_SIZE error_offset = -1;
        pcre2_code* regex =
            pcre2_compile((PCRE2_SPTR) match.c_str(), match.length(), pcre_ops,
                          &errorcode, &error_offset, NULL);

        if (regex)
        {
            // Try to compile even further for faster matching
            if (pcre2_jit_compile(regex, PCRE2_JIT_COMPLETE) < 0)
            {
                MXS_NOTICE("PCRE2 JIT compilation of pattern '%s' failed, "
                           "falling back to normal compilation.", match.c_str());
            }

            RegexToServers regex_ser(match, regex);
            if (regex_ser.add_servers(servers) == 0)
            {
                // The servers string didn't seem to contain any servers
                MXS_ERROR("Could not parse servers from string '%s'.", servers.c_str());
                error = true;
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
                error = true;
            }
            else
            {
                if (capcount > max_capcount)
                {
                    max_capcount = capcount;
                }
            }
        }
        else
        {
            MXS_ERROR("Invalid PCRE2 regular expression '%s' (position '%zu').",
                      match.c_str(), error_offset);
            MXS_PCRE2_PRINT_ERROR(errorcode);
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
 * Check whether the client IP
 * matches the configured 'source' host
 * which can have up to three % wildcards
 *
 * @param remote      The clientIP
 * @param ipv4        The client IPv4 struct
 * @return            1 for match, 0 otherwise
 */
int RegexHintFilter::check_source_host(const char* remote, const struct sockaddr_in* ipv4)
{
    int ret = 0;
    struct sockaddr_in check_ipv4;

    memcpy(&check_ipv4, ipv4, sizeof(check_ipv4));

    switch (m_source->m_netmask)
    {
    case 32:
        ret = strcmp(m_source->m_address, remote) == 0 ? 1 : 0;
        break;
    case 24:
        /* Class C check */
        check_ipv4.sin_addr.s_addr &= 0x00FFFFFF;
        break;
    case 16:
        /* Class B check */
        check_ipv4.sin_addr.s_addr &= 0x0000FFFF;
        break;
    case 8:
        /* Class A check */
        check_ipv4.sin_addr.s_addr &= 0x000000FF;
        break;
    default:
        break;
    }

    ret = (m_source->m_netmask < 32) ?
          (check_ipv4.sin_addr.s_addr == m_source->m_ipv4.sin_addr.s_addr) :
          ret;

    if (ret)
    {
        MXS_INFO("Client IP %s matches host source %s%s",
                 remote,
                 m_source->m_netmask < 32 ? "with wildcards " : "",
                 m_source->m_address);
    }

    return ret;
}

/**
 * Validate IP address string againt three dots
 * and last char not being a dot.
 *
 * Match any, '%' or '%.%.%.%', is not allowed
 *
 */
bool RegexHintFilter::validate_ip_address(const char* host)
{
    int n_dots = 0;

    /**
     * Match any is not allowed
     * Start with dot not allowed
     * Host len can't be greater than INET_ADDRSTRLEN
     */
    if (*host == '%' ||
        *host == '.' ||
        strlen(host) > INET_ADDRSTRLEN)
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
 * Set the 'source' option into a proper struct
 *
 * Input IP, which could have wildcards %, is checked
 * and the netmask 32/24/16/8 is added.
 *
 * In case of errors the 'address' field of
 * struct REGEXHINT_SOURCE_HOST is set to NULL
 *
 * @param input_host    The config source parameter
 * @return              The filled struct with netmask
 *
 */
SourceHost* RegexHintFilter::set_source_address(const char* input_host)
{
    ss_dassert(input_host);
    int netmask = 32;
    int bytes = 0;
    struct sockaddr_in serv_addr;

    if (!input_host)
    {
        return NULL;
    }

    SourceHost* source_host = new SourceHost();

    if (!validate_ip_address(input_host))
    {
        MXS_WARNING("The given 'source' parameter source=%s"
                    " is not a valid IP address: it will not be used.",
                    input_host);

        source_host->m_address = NULL;
        return source_host;
    }

    source_host->m_address = input_host;

    /* If no wildcards don't check it, set netmask to 32 and return */
    if (!strchr(input_host, '%'))
    {
        source_host->m_netmask = netmask;
        return source_host;
    }

    char format_host[strlen(input_host) + 1];
    char* p = (char*)input_host;
    char* out = format_host;

    while (*p && bytes <= 3)
    {
        if (*p == '.')
        {
            bytes++;
        }

        if (*p == '%')
        {
            *out = bytes == 3 ? '1' : '0';
            netmask -= 8;

            out++;
            p++;
        }
        else
        {
            *out++ = *p++;
        }
    }

    *out = '\0';
    source_host->m_netmask = netmask;

    struct addrinfo *ai = NULL, hint = {};
    hint.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
    int rc = getaddrinfo(input_host, NULL, &hint, &ai);

    /* fill IPv4 data struct */
    if (rc == 0)
    {
        ss_dassert(ai->ai_family == AF_INET);
        memcpy(&source_host->ipv4, ai->ai_addr, ai->ai_addrlen);

        /* if netmask < 32 there are % wildcards */
        if (source_host->m_netmask < 32)
        {
            /* let's zero the last IP byte: a.b.c.0 we may have set above to 1*/
            source_host->m_ipv4.sin_addr.s_addr &= 0x00FFFFFF;
        }

        MXS_INFO("Input %s is valid with netmask %d", source_host->address, source_host->netmask);
        freeaddrinfo(ai);
    }
    else
    {
        MXS_WARNING("Found invalid IP address for parameter 'source=%s': %s",
                    input_host, gai_strerror(rc));
        MXS_FREE(source_host->address);
        MXS_FREE(source_host);
        return NULL;
    }

    return source_host;
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
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"source", MXS_MODULE_PARAM_STRING},
            {"user", MXS_MODULE_PARAM_STRING},
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
    /* Create parameter pair names */
    generate_param_names(match_server_pairs);


    /* Now make the actual parameters for the module struct */
    MXS_MODULE_PARAM new_param = {NULL, MXS_MODULE_PARAM_STRING, NULL};
    for (unsigned int i = 0; i < param_names_match.size(); i++)
    {
        new_param.name = param_names_match.at(i).c_str();
        info.parameters[params_counter] = new_param;
        params_counter++;
        new_param.name = param_names_server.at(i).c_str();
        info.parameters[params_counter] = new_param;
        params_counter++;
    }
    info.parameters[params_counter].name = MXS_END_MODULE_PARAMS;

    return &info;
}

/* Generate N pairs of parameter names of form matchXX and serverXX
 *
 * @param pairs The number of parameter pairs to generate
 */
static void generate_param_names(int pairs)
{
    const char MATCH[] = "match";
    const char SERVER[] = "server";
    const int namelen_match = sizeof(MATCH) + 2;
    const int namelen_server = sizeof(SERVER) + 2;

    char name_match[namelen_match];
    char name_server[namelen_server];

    /* First, create the old "match" and "server" parameters for backwards
     * compatibility. */
    if (pairs > 0)
    {
        param_names_match.push_back(MATCH);
        param_names_server.push_back(SERVER);
    }
    /* Then all the rest. */
    const char FORMAT[] = "%s%02d";
    for (int counter = 1; counter < pairs; counter++)
    {
        ss_debug(int rval = ) snprintf(name_match, namelen_match, FORMAT, MATCH, counter);
        ss_dassert(rval == namelen_match - 1);
        ss_debug(rval = ) snprintf(name_server, namelen_server, FORMAT, SERVER, counter);
        ss_dassert(rval == namelen_server - 1);

        // Have both names, add them to the global vectors
        param_names_match.push_back(name_match);
        param_names_server.push_back(name_server);
    }
}

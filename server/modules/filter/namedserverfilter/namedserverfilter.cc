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

#include <maxscale/cppdefs.hh>

#include <string>
#include <string.h>
#include <stdio.h>

#include <maxscale/alloc.h>
#include <maxscale/filter.h>
#include <maxscale/hint.h>
#include <maxscale/log_manager.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/pcre2.hh>
#include <maxscale/utils.h>

using std::string;

class RegexHintInst;
struct RegexHintSess_t;

typedef struct source_host
{
    const char *address;
    struct sockaddr_in ipv4;
    int netmask;
} REGEXHINT_SOURCE_HOST;

/**
 * Instance structure
 */
class RegexHintInst : public MXS_FILTER
{
private:
    string m_match; /* Regular expression to match */
    string m_server; /* Server to route to */
    string m_user; /* User name to restrict matches */

    REGEXHINT_SOURCE_HOST *m_source; /* Source address to restrict matches */
    pcre2_code* m_regex; /* Compiled regex text, can be used from multiple threads */

    /* Total statements diverted statistics. Unreliable due to non-locked but
     * shared access. */
    volatile unsigned int m_total_diverted;
    volatile unsigned int m_total_undiverted;

    int check_source_host(const char *remote, const struct sockaddr_in *ipv4);

public:
    RegexHintInst(string match, string server, string user, REGEXHINT_SOURCE_HOST* source,
                  pcre2_code* regex);
    ~RegexHintInst();
    RegexHintSess_t* newSession(MXS_SESSION *session);
    int routeQuery(RegexHintSess_t* session, GWBUF *queue);
    void diagnostic(RegexHintSess_t* session, DCB *dcb);
};

/**
 * The session structure for this regexhint filter
 */
typedef struct RegexHintSess_t
{
    MXS_DOWNSTREAM down; /* The downstream filter */
    int n_diverted; /* No. of statements diverted */
    int n_undiverted; /* No. of statements not diverted */
    int active; /* Is filter active */
    pcre2_match_data *match_data; /* regex result container */
    bool regex_error_printed;
} RegexHintSess;

/* Api entrypoints */
static MXS_FILTER *createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params);
static MXS_FILTER_SESSION *newSession(MXS_FILTER *instance, MXS_SESSION *session);
static void closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, MXS_DOWNSTREAM *downstream);
static int routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);
static void diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb);
static uint64_t getCapabilities(MXS_FILTER* instance);
/* End entrypoints */

static bool validate_ip_address(const char *);
static REGEXHINT_SOURCE_HOST *set_source_address(const char *);
static void free_instance(RegexHintInst *);

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case", 0},
    {"extended", PCRE2_EXTENDED}, // Ignore white space and # comments
    {NULL}
};

RegexHintInst::RegexHintInst(string match, string server, string user,
                             REGEXHINT_SOURCE_HOST* source, pcre2_code* regex)
    :   m_match(match),
        m_server(server),
        m_user(user),
        m_source(source),
        m_regex(regex),
        m_total_diverted(0),
        m_total_undiverted(0)
{}
RegexHintInst::~RegexHintInst()
{
    pcre2_code_free(m_regex);
    MXS_FREE(m_source);
}

RegexHintSess_t* RegexHintInst::newSession(MXS_SESSION *session)
{
    RegexHintSess *my_session;
    const char *remote, *user;

    if ((my_session = (RegexHintSess*)MXS_CALLOC(1, sizeof(RegexHintSess))) != NULL)
    {
        my_session->n_diverted = 0;
        my_session->n_undiverted = 0;
        my_session->regex_error_printed = false;
        my_session->active = 1;
        /* It's best to generate match data from the pattern to avoid extra allocations
         * during matching. If data creation fails, matching will fail as well. */
        my_session->match_data = pcre2_match_data_create_from_pattern(m_regex, NULL);

        /* Check client IP against 'source' host option */
        if (m_source && m_source->address &&
            (remote = session_get_remote(session)) != NULL)
        {
            my_session->active =
                this->check_source_host(remote, &session->client_dcb->ipv4);
        }

        /* Check client user against 'user' option */
        if (m_user.length() &&
            (user = session_get_user(session)) &&
            (user != m_user))
        {
            my_session->active = 0;
        }
    }
    return my_session;
}

int RegexHintInst::routeQuery(RegexHintSess_t* my_session, GWBUF *queue)
{
    char *sql = NULL;
    int sql_len = 0;

    if (modutil_is_SQL(queue) && my_session->active)
    {
        if (modutil_extract_SQL(queue, &sql, &sql_len))
        {
            int result = pcre2_match(m_regex, (PCRE2_SPTR)sql, sql_len, 0, 0,
                                     my_session->match_data, NULL);
            if (result >= 0)
            {
                /* Have a match. No need to check if the regex matches the complete
                 * query, since the user can form the regex to enforce this. */
                queue->hint =
                    hint_create_route(queue->hint, HINT_ROUTE_TO_NAMED_SERVER,
                                      m_server.c_str());
                my_session->n_diverted++;
                m_total_diverted++;
            }
            else if (result == PCRE2_ERROR_NOMATCH)
            {
                my_session->n_undiverted++;
                m_total_undiverted++;
            }
            else if (result < 0)
            {
                // Print regex error only once per session
                if (!my_session->regex_error_printed)
                {
                    MXS_PCRE2_PRINT_ERROR(result);
                }
                my_session->regex_error_printed = true;
                my_session->n_undiverted++;
                m_total_undiverted++;
            }
        }
    }
    return my_session->down.routeQuery(my_session->down.instance,
                                       my_session->down.session, queue);
}

void RegexHintInst::diagnostic(RegexHintSess_t* my_session, DCB *dcb)
{
    dcb_printf(dcb, "\t\tMatch and route:           /%s/ -> %s\n",
               m_match.c_str(), m_server.c_str());
    dcb_printf(dcb, "\t\tTotal no. of queries diverted by filter (approx.):     %d\n",
               m_total_diverted);
    dcb_printf(dcb, "\t\tTotal no. of queries not diverted by filter (approx.): %d\n",
               m_total_undiverted);
    if (my_session)
    {
        dcb_printf(dcb, "\t\tNo. of queries diverted by filter: %d\n",
                   my_session->n_diverted);
        dcb_printf(dcb, "\t\tNo. of queries not diverted by filter:     %d\n",
                   my_session->n_undiverted);
    }
    if (m_source)
    {
        dcb_printf(dcb,
                   "\t\tReplacement limited to connections from     %s\n",
                   m_source->address);
    }
    if (m_user.length())
    {
        dcb_printf(dcb,
                   "\t\tReplacement limit to user           %s\n",
                   m_user.c_str());
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
int RegexHintInst::check_source_host(const char *remote, const struct sockaddr_in *ipv4)
{
    int ret = 0;
    struct sockaddr_in check_ipv4;

    memcpy(&check_ipv4, ipv4, sizeof(check_ipv4));

    switch (m_source->netmask)
    {
    case 32:
        ret = strcmp(m_source->address, remote) == 0 ? 1 : 0;
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

    ret = (m_source->netmask < 32) ?
          (check_ipv4.sin_addr.s_addr == m_source->ipv4.sin_addr.s_addr) :
          ret;

    if (ret)
    {
        MXS_INFO("Client IP %s matches host source %s%s",
                 remote,
                 m_source->netmask < 32 ? "with wildcards " : "",
                 m_source->address);
    }

    return ret;
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param params    The array of name/value pair parameters for the filter
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static MXS_FILTER*
createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params)
{
    bool error = false;

    REGEXHINT_SOURCE_HOST* source = NULL;
    /* The cfg_param cannot be changed to string because set_source_address doesn't
       copy the contents. */
    const char *cfg_param = config_get_string(params, "source");
    if (*cfg_param)
    {
        source = set_source_address(cfg_param);
        if (!source)
        {
            MXS_ERROR("Failure setting 'source' from %s", cfg_param);
            error = true;
        }
    }

    string match(config_get_string(params, "match"));
    string server(config_get_string(params, "server"));
    string user(config_get_string(params, "user"));

    int pcre_ops = config_get_enum(params, "options", option_values);
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
    }
    else
    {
        error = true;
        MXS_ERROR("Invalid PCRE2 regular expression '%s' (position '%zu').",
                  match.c_str(), error_offset);
        MXS_PCRE2_PRINT_ERROR(errorcode);
    }

    if (error)
    {
        if (source)
        {
            MXS_FREE(source);
        }
        if (regex)
        {
            pcre2_code_free(regex);
        }
        return NULL;
    }
    else
    {
        RegexHintInst* instance = NULL;
        MXS_EXCEPTION_GUARD(instance =
                                new RegexHintInst(match, server, user, source, regex));
        return instance;
    }

}



/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION *
newSession(MXS_FILTER *instance, MXS_SESSION *session)
{
    RegexHintInst* my_instance = static_cast<RegexHintInst*>(instance);
    RegexHintSess* my_session = NULL;
    MXS_EXCEPTION_GUARD(my_session = my_instance->newSession(session));
    return (MXS_FILTER_SESSION*)my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
}

/**
 * Free the memory associated with this filter session.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
    RegexHintSess_t* my_session = (RegexHintSess_t*)session;
    pcre2_match_data_free(my_session->match_data);
    MXS_FREE(my_session);
    return;
}

/**
 * Set the downstream component for this filter.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 * @param downstream    The downstream filter or router
 */
static void
setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session, MXS_DOWNSTREAM *downstream)
{
    RegexHintSess *my_session = (RegexHintSess *) session;
    my_session->down = *downstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * If the regular expressed configured in the match parameter of the
 * filter definition matches the SQL text then add the hint
 * "Route to named server" with the name defined in the server parameter
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
static int
routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *session, GWBUF *queue)
{
    RegexHintInst *my_instance = static_cast<RegexHintInst*>(instance);
    RegexHintSess *my_session = (RegexHintSess *) session;
    int rval = 0;
    MXS_EXCEPTION_GUARD(rval = my_instance->routeQuery(my_session, queue));
    return rval;
}

/**
 * Diagnostics routine
 *
 * If fsession is NULL then print diagnostics on the filter
 * instance as a whole, otherwise print diagnostics for the
 * particular session.
 *
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 * @param   dcb     The DCB for diagnostic output
 */
static void
diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb)
{
    RegexHintInst *my_instance = static_cast<RegexHintInst*>(instance);
    RegexHintSess *my_session = (RegexHintSess *) fsession;
    my_instance->diagnostic(my_session, dcb);
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(MXS_FILTER* instance)
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}

/**
 * Validate IP address string againt three dots
 * and last char not being a dot.
 *
 * Match any, '%' or '%.%.%.%', is not allowed
 *
 */
static bool validate_ip_address(const char *host)
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
static REGEXHINT_SOURCE_HOST *set_source_address(const char *input_host)
{
    int netmask = 32;
    int bytes = 0;
    struct sockaddr_in serv_addr;
    REGEXHINT_SOURCE_HOST *source_host =
        (REGEXHINT_SOURCE_HOST*)MXS_CALLOC(1, sizeof(REGEXHINT_SOURCE_HOST));

    if (!input_host || !source_host)
    {
        return NULL;
    }

    if (!validate_ip_address(input_host))
    {
        MXS_WARNING("The given 'source' parameter source=%s"
                    " is not a valid IP address: it will not be used.",
                    input_host);

        source_host->address = NULL;
        return source_host;
    }

    source_host->address = input_host;

    /* If no wildcards don't check it, set netmask to 32 and return */
    if (!strchr(input_host, '%'))
    {
        source_host->netmask = netmask;
        return source_host;
    }

    char format_host[strlen(input_host) + 1];
    char *p = (char *)input_host;
    char *out = format_host;

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
    source_host->netmask = netmask;

    /* fill IPv4 data struct */
    if (setipaddress(&source_host->ipv4.sin_addr, format_host) && strlen(format_host))
    {

        /* if netmask < 32 there are % wildcards */
        if (source_host->netmask < 32)
        {
            /* let's zero the last IP byte: a.b.c.0 we may have set above to 1*/
            source_host->ipv4.sin_addr.s_addr &= 0x00FFFFFF;
        }

        MXS_INFO("Input %s is valid with netmask %d\n",
                 source_host->address,
                 source_host->netmask);
    }
    else
    {
        MXS_WARNING("Found invalid IP address for parameter 'source=%s',"
                    " it will not be used.",
                    input_host);
        source_host->address = NULL;
    }

    return (REGEXHINT_SOURCE_HOST *)source_host;
}

/**
 * Free allocated memory
 *
 * @param instance    The filter instance
 */
static void free_instance(RegexHintInst *instance)
{
    MXS_EXCEPTION_GUARD(delete instance);
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
    static MXS_FILTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        setDownstream,
        NULL, // No Upstream requirement
        routeQuery,
        NULL, // No clientReply
        diagnostic,
        getCapabilities,
        NULL, // No destroyInstance
    };

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
            {"match", MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
            {"server", MXS_MODULE_PARAM_SERVER, NULL, MXS_MODULE_OPT_REQUIRED},
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

    return &info;
}

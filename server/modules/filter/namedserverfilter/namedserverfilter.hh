#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>

#include <string>
#include <vector>
#include <netdb.h>

#include <maxscale/filter.hh>
#include <maxscale/buffer.hh>
#include <maxscale/pcre2.hh>
#include <maxscale/hint.h>

class RegexHintFilter;
class RegexHintFSession;

struct RegexToServers;
struct SourceHost;

using std::string;
typedef std::vector<string> StringArray;
typedef std::vector<RegexToServers> MappingArray;

/**
 * Filter instance definition
 */
class RegexHintFilter : public maxscale::Filter<RegexHintFilter, RegexHintFSession>
{
private:
    const string m_user; /* User name to restrict matches with */
    SourceHost* m_source; /* Source address to restrict matches */
    MappingArray m_mapping; /* Regular expression to serverlist mapping */
    const int m_ovector_size; /* Given to pcre2_match_data_create() */

    int check_source_host(const char *remote, const struct sockaddr_storage *ip);
public:
    /* Total statements diverted statistics. Unreliable due to lockless yet
     * shared access. */
    volatile unsigned int m_total_diverted;
    volatile unsigned int m_total_undiverted;

    RegexHintFilter(string user, SourceHost* source, const MappingArray& map,
                    int ovector_size);
    ~RegexHintFilter();
    static RegexHintFilter* create(const char* zName,  MXS_CONFIG_PARAMETER* ppParams);
    RegexHintFSession* newSession(MXS_SESSION *session);
    void diagnostics(DCB* dcb);
    json_t* diagnostics_json() const;
    uint64_t getCapabilities();
    const RegexToServers* find_servers(char* sql, int sql_len, pcre2_match_data* mdata);

    static void form_regex_server_mapping(MXS_CONFIG_PARAMETER* params, int pcre_ops,
                                          MappingArray* mapping, uint32_t* max_capcount_out);
    static bool regex_compile_and_add(int pcre_ops, bool legacy_mode, const string& match,
                                      const string& servers, MappingArray* mapping, uint32_t* max_capcount);
    static bool validate_ip_address(const char *);
    static SourceHost* set_source_address(const char *);
};

/**
 * The session structure for the regexhint (namedserver) filter
 */
class RegexHintFSession : public maxscale::FilterSession
{
private:
    MXS_SESSION* m_session; /* The main client session */
    RegexHintFilter& m_fil_inst; /* Filter instance */
    int m_n_diverted; /* No. of statements diverted */
    int m_n_undiverted; /* No. of statements not diverted */
    int m_active; /* Is filter active? */
    pcre2_match_data *m_match_data; /* compiled regex */
public:
    RegexHintFSession(MXS_SESSION* session, RegexHintFilter& filter, bool active,
                      pcre2_match_data* md);
    ~RegexHintFSession();

    void diagnostics(DCB* pDcb);
    json_t* diagnostics_json() const;
    int routeQuery(GWBUF* buffer);
};

/* Storage class which maps a regex to a set of servers. Note that this struct
 * does not manage the regex memory. That is done by the filter instance. */
struct RegexToServers
{
    string m_match; /* Regex in text form */
    pcre2_code* m_regex; /* Compiled regex */
    StringArray m_targets; /* List of target servers. */
    HINT_TYPE m_htype; /* For special hint types */
    volatile bool m_error_printed; /* Has an error message about
                                    * matching this regex been printed yet? */
    RegexToServers(string match, pcre2_code* regex)
        : m_match(match),
          m_regex(regex),
          m_htype(HINT_ROUTE_TO_NAMED_SERVER),
          m_error_printed(false)
    {}

    int add_servers(string server_names, bool legacy_mode);
};

/* Container for address-specific filtering */
struct SourceHost
{
    string m_address;
    struct sockaddr_in m_ipv4;
    int m_netmask;
    SourceHost(string address, const struct sockaddr_in& ipv4, int netmask)
        : m_address(address),
          m_ipv4(ipv4),
          m_netmask(netmask)
    {}
};

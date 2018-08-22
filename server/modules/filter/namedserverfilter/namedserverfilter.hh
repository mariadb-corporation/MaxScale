/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
 #pragma once

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

using StringVector = std::vector<std::string>;
using MappingVector = std::vector<RegexToServers>;
using SourceHostVector = std::vector<SourceHost>;

/**
 * Filter instance definition
 */
class RegexHintFilter : public maxscale::Filter<RegexHintFilter, RegexHintFSession>
{
private:
    const std::string m_user; /* User name to restrict matches with */
    SourceHostVector m_sources; /* Source addresses to restrict matches */
    StringVector m_hostnames;  /* Source hostnames to restrict matches */
    MappingVector m_mapping; /* Regular expression to serverlist mapping */
    const int m_ovector_size; /* Given to pcre2_match_data_create() */

    bool check_source_host(const char *remote, const struct sockaddr_storage *ip);
    bool check_source_hostnames(const char *remote, const struct sockaddr_storage *ip);
    /* Change ipv6 mapped ipv4 address to actual ipv4 address*/
    void mapped_ipv6_to_ipv4(struct sockaddr_storage* ip);
public:
    /* Total statements diverted statistics. Unreliable due to lockless yet
     * shared access. */
    volatile unsigned int m_total_diverted;
    volatile unsigned int m_total_undiverted;

    RegexHintFilter(const std::string& user, const SourceHostVector& source, const StringVector& hostnames, const MappingVector& map,
                    int ovector_size);
    ~RegexHintFilter();
    static RegexHintFilter* create(const char* zName,  MXS_CONFIG_PARAMETER* ppParams);
    RegexHintFSession* newSession(MXS_SESSION *session);
    void diagnostics(DCB* dcb);
    json_t* diagnostics_json() const;
    uint64_t getCapabilities();
    const RegexToServers* find_servers(char* sql, int sql_len, pcre2_match_data* mdata);

    static void form_regex_server_mapping(MXS_CONFIG_PARAMETER* params, int pcre_ops,
                                          MappingVector* mapping, uint32_t* max_capcount_out);
    static bool regex_compile_and_add(int pcre_ops, bool legacy_mode, const std::string& match,
                                      const std::string& servers, MappingVector* mapping, uint32_t* max_capcount);
    static bool validate_ip_address(const char *);
    static bool add_source_address(const char *, SourceHostVector&);
    static bool set_source_addresses(const std::string& input_host_names, SourceHostVector&, StringVector&);
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
    std::string m_match; /* Regex in text form */
    pcre2_code* m_regex; /* Compiled regex */
    StringVector m_targets; /* List of target servers. */
    HINT_TYPE m_htype; /* For special hint types */
    volatile bool m_error_printed; /* Has an error message about
                                    * matching this regex been printed yet? */
    RegexToServers(const std::string& match, pcre2_code* regex)
        : m_match(match),
          m_regex(regex),
          m_htype(HINT_ROUTE_TO_NAMED_SERVER),
          m_error_printed(false)
    {}

    int add_servers(const std::string& server_names, bool legacy_mode);
};

/* Container for address-specific filtering */
struct SourceHost
{
    std::string m_address;
    struct sockaddr_in m_ipv4;
    int m_netmask;
    SourceHost(std::string address, const struct sockaddr_in& ipv4, int netmask)
        : m_address(address),
          m_ipv4(ipv4),
          m_netmask(netmask)
    {}
};

/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
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
#include <maxscale/config2.hh>
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
class RegexHintFilter : public mxs::Filter
{
public:
    /* Total statements diverted statistics. Unreliable due to lockless yet
     * shared access. */
    volatile unsigned int m_total_diverted {0};
    volatile unsigned int m_total_undiverted {0};

    RegexHintFilter(const std::string& name);

    static RegexHintFilter*     create(const char* zName, mxs::ConfigParameters* ppParams);
    mxs::FilterSession*         newSession(MXS_SESSION* session, SERVICE* service);
    json_t*                     diagnostics() const;
    uint64_t                    getCapabilities() const;
    mxs::config::Configuration* getConfiguration();


    MappingVector& mapping();
    int            ovector_size() const;

private:

    class Settings : public mxs::config::Configuration
    {
    public:
        explicit Settings(const std::string& name);

        static constexpr int n_regex_max {25};

        std::string m_user;
        std::string m_source;
        uint32_t    m_regex_options {0};

        // Legacy params
        std::string m_match;
        std::string m_server;

        // Indexed params
        struct MatchAndTarget
        {
            std::string match;
            std::string target;
        };
        MatchAndTarget m_match_targets[n_regex_max];
    };

    SourceHostVector m_sources;         /* Source addresses to restrict matches */
    StringVector     m_hostnames;       /* Source hostnames to restrict matches */
    MappingVector    m_mapping;         /* Regular expression to serverlist mapping */
    int              m_ovector_size {1};/* Given to pcre2_match_data_create() */

    Settings m_settings;

    bool check_source_host(const char* remote, const struct sockaddr_storage* ip);
    bool check_source_hostnames(const struct sockaddr_storage* ip);
    bool configure(mxs::ConfigParameters* params);
    void set_source_addresses(const std::string& input_host_names);
    bool add_source_address(const std::string& input_host);
    void form_regex_server_mapping(int pcre_ops);
    bool regex_compile_and_add(int pcre_ops, bool legacy_mode, const std::string& match,
                               const std::string& target);
    static bool validate_ipv4_address(const char*);
};

/**
 * The session structure for the regexhint (namedserver) filter
 */
class RegexHintFSession : public maxscale::FilterSession
{
public:
    RegexHintFSession(MXS_SESSION* session, SERVICE* service, RegexHintFilter& filter, bool active);
    ~RegexHintFSession();

    json_t* diagnostics() const;
    int     routeQuery(GWBUF* buffer);

private:
    RegexHintFilter&  m_fil_inst;
    pcre2_match_data* m_match_data {nullptr};

    int m_n_diverted {0};       /* No. of statements diverted */
    int m_n_undiverted {0};     /* No. of statements not diverted */
    int m_active;               /* Is filter active? */

    const RegexToServers* find_servers(char* sql, int sql_len);
};

/* Storage class which maps a regex to a set of servers. Note that this struct
 * does not manage the regex memory. That is done by the filter instance. */
struct RegexToServers
{
    RegexToServers(const RegexToServers&) = delete;
    RegexToServers& operator=(const RegexToServers&) = delete;

    std::string  m_match;                               /* Regex in text form */
    pcre2_code*  m_regex {nullptr};                     /* Compiled regex */
    StringVector m_targets;                             /* List of target servers or a special tag. */
    HINT_TYPE    m_htype {HINT_ROUTE_TO_NAMED_SERVER};  /* Hint type */

    /* Has an error message about matching this regex been printed yet? */
    std::atomic_bool m_error_printed {false};

    RegexToServers(const std::string& match, pcre2_code* regex)
        : m_match(match)
        , m_regex(regex)
    {
    }
    RegexToServers(RegexToServers&& rhs) noexcept;
    ~RegexToServers();

    bool add_targets(const std::string& target, bool legacy_mode);
};

/* Container for address-specific filtering */
struct SourceHost
{
    std::string         m_address;
    struct sockaddr_in6 m_ipv6;
    int                 m_netmask;
    SourceHost(const std::string& address, const struct sockaddr_in6& ipv6, int netmask)
        : m_address(address)
        , m_ipv6(ipv6)
        , m_netmask(netmask)
    {
    }
};

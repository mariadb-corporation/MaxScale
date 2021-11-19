/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <string>
#include <vector>
#include <memory>
#include <netdb.h>

#include <maxscale/filter.hh>
#include <maxscale/buffer.hh>
#include <maxscale/config2.hh>
#include <maxscale/pcre2.hh>
#include <maxscale/hint.h>
#include <maxscale/workerlocal.hh>

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

    struct Setup
    {
        SourceHostVector sources;           /* Source addresses to restrict matches */
        StringVector     hostnames;         /* Source hostnames to restrict matches */
        MappingVector    mapping;           /* Regular expression to serverlist mapping */
        int              ovector_size {1};  /* Given to pcre2_match_data_create() */
    };

    RegexHintFilter(const std::string& name);

    static RegexHintFilter*     create(const char* zName);
    mxs::FilterSession*         newSession(MXS_SESSION* session, SERVICE* service) override;
    json_t*                     diagnostics() const override;
    uint64_t                    getCapabilities() const override;
    mxs::config::Configuration& getConfiguration() override;

    bool post_configure();

private:

    class Settings : public mxs::config::Configuration
    {
    public:
        explicit Settings(const std::string& name, RegexHintFilter* filter);

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

    protected:
        bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override
        {
            return m_filter->post_configure();
        }

        RegexHintFilter* m_filter;
    };

    Settings m_settings;

    mxs::WorkerGlobal<std::shared_ptr<Setup>> m_setup;

    bool check_source_host(const std::shared_ptr<Setup>& setup,
                           const char* remote, const struct sockaddr_storage* ip);
    bool check_source_hostnames(const std::shared_ptr<Setup>& setup, const struct sockaddr_storage* ip);
    void set_source_addresses(const std::shared_ptr<Setup>& setup, const std::string& input_host_names);
    bool add_source_address(const std::shared_ptr<Setup>& setup, const std::string& input_host);
    bool form_regex_server_mapping(const std::shared_ptr<Setup>& setup, int pcre_ops);
    bool regex_compile_and_add(const std::shared_ptr<Setup>& setup,
                               int pcre_ops, bool legacy_mode, const std::string& match,
                               const std::string& target);
    static bool validate_ipv4_address(const char*);
};

/**
 * The session structure for the regexhint (namedserver) filter
 */
class RegexHintFSession : public maxscale::FilterSession
{
public:
    RegexHintFSession(MXS_SESSION* session, SERVICE* service, RegexHintFilter& filter, bool active,
                      std::shared_ptr<RegexHintFilter::Setup>&& setup);
    ~RegexHintFSession();

    json_t* diagnostics() const;
    bool    routeQuery(GWBUF* buffer) override;
    bool    clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

private:
    RegexHintFilter&  m_fil_inst;
    pcre2_match_data* m_match_data {nullptr};

    int  m_n_diverted {0};      /* No. of statements diverted */
    int  m_n_undiverted {0};    /* No. of statements not diverted */
    bool m_active {true};       /* Is filter active? */

    /** Maps COM_STMT_PREPARE IDs to a list of hints. */
    std::unordered_map<uint32_t, HINT*> m_ps_id_to_hints;

    uint32_t m_current_prep_id {0};     /**< ID of the PS currently preparing on server */
    uint32_t m_last_prepare_id {0};     /**< Last id prepared */

    std::shared_ptr<RegexHintFilter::Setup> m_setup;

    const RegexToServers* find_servers(char* sql, int sql_len);
    void                  free_hint_list(HINT** hlist);
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

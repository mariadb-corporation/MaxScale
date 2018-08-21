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
#include <memory>
#include <memory>
#include <maxscale/buffer.hh>
#include <maxscale/filter.hh>
#include "maskingrules.hh"

class MaskingFilter;
class MaskingFilterConfig;

class MaskingFilterSession : public maxscale::FilterSession
{
public:
    typedef MaskingFilterConfig Config;

    ~MaskingFilterSession();

    static MaskingFilterSession* create(MXS_SESSION* pSession, const MaskingFilter* pFilter);

    int routeQuery(GWBUF* pPacket);

    int clientReply(GWBUF* pPacket);

private:
    MaskingFilterSession(MXS_SESSION* pSession, const MaskingFilter* pFilter);

    MaskingFilterSession(const MaskingFilterSession&);
    MaskingFilterSession& operator = (const MaskingFilterSession&);

    enum state_t
    {
        EXPECTING_NOTHING,
        EXPECTING_RESPONSE,
        EXPECTING_FIELD,
        EXPECTING_FIELD_EOF,
        EXPECTING_ROW,
        EXPECTING_ROW_EOF,
        IGNORING_RESPONSE,
        SUPPRESSING_RESPONSE
    };

    void handle_response(GWBUF* pPacket);
    void handle_field(GWBUF* pPacket);
    void handle_row(GWBUF* pPacket);
    void handle_eof(GWBUF* pPacket);
    void handle_large_payload();

    void mask_values(ComPacket& response);

    bool reject_if_function_used(GWBUF* pPacket);

private:
    typedef std::shared_ptr<MaskingRules> SMaskingRules;

    class ResponseState
    {
    public:
        ResponseState()
            : m_command(0)
            , m_nTotal_fields(0)
            , m_index(0)
            , m_multi_result(false)
            , m_some_rule_matches(false)
        {}

        void reset(uint8_t command, const SMaskingRules& sRules)
        {
            reset_multi();

            m_command = command;
            m_sRules = sRules;
            m_multi_result = false;
            m_some_rule_matches = false;
        }

        void reset_multi()
        {
            m_nTotal_fields = 0;
            m_types.clear();
            m_rules.clear();
            m_index = 0;
            m_multi_result = true;
        }

        uint8_t command() const
        {
            return m_command;
        }

        const SMaskingRules& rules() const
        {
            return m_sRules;
        }

        bool some_rule_matches() const
        {
            return m_some_rule_matches;
        }

        bool is_multi_result() const
        {
            return m_multi_result;
        }

        uint32_t total_fields() const
        {
            return m_nTotal_fields;
        }

        void set_total_fields(uint32_t n)
        {
            m_nTotal_fields = n;
        }

        bool append_type_and_rule(enum_field_types type, const MaskingRules::Rule* pRule)
        {
            m_types.push_back(type);
            m_rules.push_back(pRule);

            if (pRule)
            {
                m_some_rule_matches = true;
            }

            return m_rules.size() == m_nTotal_fields;
        }

        const std::vector<enum_field_types>& types() const
        {
            return m_types;
        }

        const MaskingRules::Rule* get_rule()
        {
            ss_dassert(m_nTotal_fields == m_rules.size());
            ss_dassert(m_index < m_rules.size());
            const MaskingRules::Rule* pRule = m_rules[m_index++];
            // The rules will be used repeatedly for each row. Hence, once we hit
            // the end, we need to continue from the start.
            m_index = m_index % m_rules.size();
            return pRule;
        }

    private:
        uint8_t                                m_command;           /*<! What command. */
        SMaskingRules                          m_sRules;            /*<! The rules that are used. */
        uint32_t                               m_nTotal_fields;     /*<! The total number of fields. */
        std::vector<enum_field_types>          m_types;             /*<! The column types. */
        std::vector<const MaskingRules::Rule*> m_rules;             /*<! The rules applied for columns. */
        size_t                                 m_index;             /*<! Index to the current rule.*/
        bool                                   m_multi_result;      /*<! Are we processing multi-results. */
        bool                                   m_some_rule_matches; /*<! At least one rule matches. */
    };

    const MaskingFilter& m_filter;
    state_t              m_state;
    ResponseState        m_res;
};

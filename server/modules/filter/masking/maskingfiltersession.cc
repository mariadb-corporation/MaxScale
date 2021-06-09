/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "masking"
#include "maskingfiltersession.hh"

#include <sstream>

#include <maxscale/buffer.hh>
#include <maxscale/filter.hh>
#include <maxscale/modutil.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/poll.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>

#include "maskingfilter.hh"
#include "mysql.hh"

using maxscale::Buffer;
using std::ostream;
using std::string;
using std::stringstream;

namespace
{

GWBUF* create_error_response(const char* zMessage)
{
    return modutil_create_mysql_err_msg(1, 0, 1141, "HY000", zMessage);
}

GWBUF* create_parse_error_response()
{
    const char* zMessage =
        "The statement could not be fully parsed and will hence be "
        "rejected (masking filter).";

    return create_error_response(zMessage);
}

// TODO: In 2.4 move to query_classifier.hh.
class EnableOption
{
public:
    EnableOption(const EnableOption&) = delete;
    EnableOption& operator=(const EnableOption&) = delete;

    EnableOption(uint32_t option)
        : m_option(option)
        , m_options(0)
        , m_disable(false)
    {
        if (m_option)
        {
            m_options = qc_get_options();

            if (!(m_options & m_option))
            {
                uint32_t options = (m_options | m_option);
                MXB_AT_DEBUG(bool rv = ) qc_set_options(options);
                mxb_assert(rv);
                m_disable = true;
            }
        }
    }

    ~EnableOption()
    {
        if (m_disable)
        {
            MXB_AT_DEBUG(bool rv = ) qc_set_options(m_options);
            mxb_assert(rv);
        }
    }

private:
    uint32_t m_option;
    uint32_t m_options;
    bool     m_disable;
};

bool should_be_masked(enum_field_types type)
{
    switch (type)
    {
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_STRING:
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_VAR_STRING:
        return true;

    case MYSQL_TYPE_BIT:
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_ENUM:
    case MYSQL_TYPE_GEOMETRY:
    case MYSQL_TYPE_NEWDECIMAL:
    case MYSQL_TYPE_SET:
        // These, although returned as length-encoded strings, also in the case of
        // a binary resultset row, are not are not considered to be strings from the
        // perspective of masking.
        return false;

    default:
        // Nothing else is considered to be strings even though, in the case of
        // a textual resultset, that's what they all are.
        return false;
    }
}
}

MaskingFilterSession::MaskingFilterSession(MXS_SESSION* pSession,
                                           SERVICE* pService,
                                           const MaskingFilter* pFilter)
    : maxscale::FilterSession(pSession, pService)
    , m_filter(*pFilter)
    , m_state(IGNORING_RESPONSE)
{
}

MaskingFilterSession::~MaskingFilterSession()
{
}

// static
MaskingFilterSession* MaskingFilterSession::create(MXS_SESSION* pSession,
                                                   SERVICE* pService,
                                                   const MaskingFilter* pFilter)
{
    return new MaskingFilterSession(pSession, pService, pFilter);
}

bool MaskingFilterSession::check_query(GWBUF* pPacket)
{
    const char* zUser = session_get_user(m_pSession);
    const char* zHost = session_get_remote(m_pSession);

    if (!zUser)
    {
        zUser = "";
    }

    if (!zHost)
    {
        zHost = "";
    }

    bool acceptable = true;

    const MaskingFilter::Config& config = m_filter.config();

    if (qc_query_is_type(qc_get_type_mask(pPacket), QUERY_TYPE_USERVAR_WRITE))
    {
        if (config.check_user_variables())
        {
            if (is_variable_defined(pPacket, zUser, zHost))
            {
                acceptable = false;
            }
        }
    }
    else
    {
        qc_query_op_t op = qc_get_operation(pPacket);

        if (op == QUERY_OP_SELECT)
        {
            if (config.check_unions() || config.check_subqueries())
            {
                if (is_union_or_subquery_used(pPacket, zUser, zHost))
                {
                    acceptable = false;
                }
            }
        }

        if (acceptable && config.prevent_function_usage())
        {
            if (is_function_used(pPacket, zUser, zHost))
            {
                acceptable = false;
            }
        }
    }

    return acceptable;
}

bool MaskingFilterSession::check_textual_query(GWBUF* pPacket)
{
    bool rv = false;

    uint32_t option = m_filter.config().treat_string_arg_as_field() ? QC_OPTION_STRING_ARG_AS_FIELD : 0;
    EnableOption enable(option);

    if (qc_parse(pPacket, QC_COLLECT_FIELDS | QC_COLLECT_FUNCTIONS) == QC_QUERY_PARSED
        || !m_filter.config().require_fully_parsed())
    {
        if (qc_query_is_type(qc_get_type_mask(pPacket), QUERY_TYPE_PREPARE_NAMED_STMT))
        {
            GWBUF* pP = qc_get_preparable_stmt(pPacket);

            if (pP)
            {
                rv = check_textual_query(pP);
            }
            else
            {
                // If pP is NULL, it indicates that we have a "prepare ps from @a". It must
                // be rejected as we currently have no means for checking what columns are
                // referred to.
                const char* zMessage =
                    "A statement prepared from a variable is rejected (masking filter).";

                set_response(create_error_response(zMessage));
            }
        }
        else
        {
            rv = check_query(pPacket);
        }
    }
    else
    {
        set_response(create_parse_error_response());
    }

    return rv;
}

bool MaskingFilterSession::check_binary_query(GWBUF* pPacket)
{
    bool rv = false;

    uint32_t option = m_filter.config().treat_string_arg_as_field() ? QC_OPTION_STRING_ARG_AS_FIELD : 0;
    EnableOption enable(option);

    if (qc_parse(pPacket, QC_COLLECT_FIELDS | QC_COLLECT_FUNCTIONS) == QC_QUERY_PARSED
        || !m_filter.config().require_fully_parsed())
    {
        rv = check_query(pPacket);
    }
    else
    {
        set_response(create_parse_error_response());
    }

    return rv;
}

bool MaskingFilterSession::routeQuery(GWBUF* pPacket)
{
    ComRequest request(pPacket);

    // TODO: Breaks if responses are not waited for, before the next request is sent.
    switch (request.command())
    {
    case MXS_COM_QUERY:
        m_res.reset(request.command(), m_filter.rules());

        if (m_filter.config().is_parsing_needed())
        {
            if (check_textual_query(pPacket))
            {
                m_state = EXPECTING_RESPONSE;
            }
            else
            {
                m_state = EXPECTING_NOTHING;
            }
        }
        else
        {
            m_state = EXPECTING_RESPONSE;
        }
        break;

    case MXS_COM_STMT_PREPARE:
        if (m_filter.config().is_parsing_needed())
        {
            if (check_binary_query(pPacket))
            {
                m_state = IGNORING_RESPONSE;
            }
            else
            {
                m_state = EXPECTING_NOTHING;
            }
        }
        else
        {
            m_state = IGNORING_RESPONSE;
        }
        break;

    case MXS_COM_STMT_EXECUTE:
        m_res.reset(request.command(), m_filter.rules());
        m_state = EXPECTING_RESPONSE;
        break;

    default:
        m_state = IGNORING_RESPONSE;
    }

    int rv = 1;

    if (m_state != EXPECTING_NOTHING)
    {
        rv = FilterSession::routeQuery(pPacket);
    }
    else
    {
        gwbuf_free(pPacket);
    }

    return rv;
}

bool MaskingFilterSession::clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb_assert(gwbuf_is_contiguous(pPacket));

    ComResponse response(pPacket);

    if (response.is_err())
    {
        // If we get an error response, we just abort what we were doing.
        m_state = EXPECTING_NOTHING;
    }
    else
    {
        switch (m_state)
        {
        case EXPECTING_NOTHING:
            MXS_WARNING("Received data, although expected nothing.");

        case IGNORING_RESPONSE:
            break;

        case EXPECTING_RESPONSE:
            handle_response(pPacket);
            break;

        case EXPECTING_FIELD:
            handle_field(pPacket);
            break;

        case EXPECTING_ROW:
            handle_row(pPacket);
            break;

        case EXPECTING_FIELD_EOF:
        case EXPECTING_ROW_EOF:
            handle_eof(pPacket);
            break;

        case SUPPRESSING_RESPONSE:
            break;
        }
    }

    // The state may change by the code above, so need to check it again.
    int rv;
    if (m_state != SUPPRESSING_RESPONSE)
    {
        rv = FilterSession::clientReply(pPacket, down, reply);
    }
    else
    {
        // TODO: The return value should mean something.
        rv = 0;
    }

    return rv;
}

void MaskingFilterSession::handle_response(GWBUF* pPacket)
{
    ComResponse response(pPacket);

    switch (response.type())
    {
    case ComResponse::OK_PACKET:
        {
            ComOK ok(response);

            if (ok.status() & SERVER_MORE_RESULTS_EXIST)
            {
                m_res.reset_multi();
                m_state = EXPECTING_RESPONSE;
            }
            else
            {
                m_state = EXPECTING_NOTHING;
            }
        }
        break;

    case ComResponse::LOCAL_INFILE_PACKET:      // GET_MORE_CLIENT_DATA/SEND_MORE_CLIENT_DATA
        m_state = EXPECTING_NOTHING;
        break;

    default:
        {
            ComQueryResponse query_response(response);

            m_res.set_total_fields(query_response.nFields());
            m_state = EXPECTING_FIELD;
        }
    }
}

void MaskingFilterSession::handle_field(GWBUF* pPacket)
{
    ComQueryResponse::ColumnDef column_def(pPacket);

    if (column_def.payload_len() >= ComPacket::MAX_PAYLOAD_LEN)     // Not particularly likely...
    {
        handle_large_payload();
    }
    else
    {
        const char* zUser = session_get_user(m_pSession);
        const char* zHost = session_get_remote(m_pSession);

        if (!zUser)
        {
            zUser = "";
        }

        if (!zHost)
        {
            zHost = "";
        }

        const MaskingRules::Rule* pRule = m_res.rules()->get_rule_for(column_def, zUser, zHost);

        if (m_res.append_type_and_rule(column_def.type(), pRule))
        {
            // All fields have been read.
            m_state = EXPECTING_FIELD_EOF;
        }
    }
}

void MaskingFilterSession::handle_eof(GWBUF* pPacket)
{
    ComResponse response(pPacket);

    if (response.is_eof())
    {
        switch (m_state)
        {
        case EXPECTING_FIELD_EOF:
            m_state = EXPECTING_ROW;
            break;

        case EXPECTING_ROW_EOF:
            m_state = EXPECTING_NOTHING;
            break;

        default:
            mxb_assert(!true);
            m_state = IGNORING_RESPONSE;
        }
    }
    else
    {
        MXS_ERROR("Expected EOF, got something else: %d", response.type());
        m_state = IGNORING_RESPONSE;
    }
}

namespace
{

void warn_of_type_mismatch(const MaskingRules::Rule& rule)
{
    MXS_WARNING("The rule targeting \"%s\" matches a column "
                "that is not of string type.",
                rule.match().c_str());
}
}

void MaskingFilterSession::handle_row(GWBUF* pPacket)
{
    ComPacket response(pPacket);

    if ((response.payload_len() == ComEOF::PAYLOAD_LEN)
        && (ComResponse(response).type() == ComResponse::EOF_PACKET))
    {
        // EOF after last row.
        ComEOF eof(response);

        if (eof.status() & SERVER_MORE_RESULTS_EXIST)
        {
            m_res.reset_multi();
            m_state = EXPECTING_RESPONSE;
        }
        else
        {
            m_state = EXPECTING_NOTHING;
        }
    }
    else
    {
        if (m_res.some_rule_matches())
        {
            if (response.payload_len() >= ComPacket::MAX_PAYLOAD_LEN)
            {
                handle_large_payload();
            }
            else
            {
                mask_values(response);
            }
        }
    }
}

void MaskingFilterSession::handle_large_payload()
{
    if (m_filter.config().large_payload() == Config::LARGE_ABORT)
    {
        MXS_WARNING("Payload > 16MB, closing the connection.");
        m_pSession->kill();
        m_state = SUPPRESSING_RESPONSE;
    }
    else
    {
        MXS_WARNING("Payload > 16MB, no masking is performed.");
        m_state = IGNORING_RESPONSE;
    }
}

void MaskingFilterSession::mask_values(ComPacket& response)
{
    switch (m_res.command())
    {
    case MXS_COM_QUERY:
        {
            ComQueryResponse::TextResultsetRow row(response, m_res.types());

            ComQueryResponse::TextResultsetRow::iterator i = row.begin();
            while (i != row.end())
            {
                const MaskingRules::Rule* pRule = m_res.get_rule();

                if (pRule)
                {
                    ComQueryResponse::TextResultsetRow::Value value = *i;

                    if (should_be_masked(value.type()))
                    {
                        LEncString s = value.as_string();
                        pRule->rewrite(s);
                    }
                    else if (m_filter.config().warn_type_mismatch() == Config::WARN_ALWAYS)
                    {
                        warn_of_type_mismatch(*pRule);
                    }
                }
                ++i;
            }
        }
        break;

    case MXS_COM_STMT_EXECUTE:
        {
            ComQueryResponse::BinaryResultsetRow row(response, m_res.types());

            ComQueryResponse::BinaryResultsetRow::iterator i = row.begin();
            while (i != row.end())
            {
                const MaskingRules::Rule* pRule = m_res.get_rule();

                if (pRule)
                {
                    ComQueryResponse::BinaryResultsetRow::Value value = *i;

                    if (should_be_masked(value.type()))
                    {
                        LEncString s = value.as_string();
                        pRule->rewrite(s);
                    }
                    else if (m_filter.config().warn_type_mismatch() == Config::WARN_ALWAYS)
                    {
                        warn_of_type_mismatch(*pRule);
                    }
                }
                ++i;
            }
        }
        break;

    default:
        MXS_ERROR("Unexpected request: %d", m_res.command());
        mxb_assert(!true);
    }
}

bool MaskingFilterSession::is_function_used(GWBUF* pPacket, const char* zUser, const char* zHost)
{
    bool is_used = false;

    SMaskingRules sRules = m_filter.rules();

    auto pred1 = [&sRules, zUser, zHost](const QC_FIELD_INFO& field_info) {
            const MaskingRules::Rule* pRule = sRules->get_rule_for(field_info, zUser, zHost);

            return pRule ? true : false;
        };

    auto pred2 = [&sRules, zUser, zHost, &pred1](const QC_FUNCTION_INFO& function_info) {
            const QC_FIELD_INFO* begin = function_info.fields;
            const QC_FIELD_INFO* end = begin + function_info.n_fields;

            auto i = std::find_if(begin, end, pred1);

            return i != end;
        };

    const QC_FUNCTION_INFO* pInfos;
    size_t nInfos;

    qc_get_function_info(pPacket, &pInfos, &nInfos);

    const QC_FUNCTION_INFO* begin = pInfos;
    const QC_FUNCTION_INFO* end = begin + nInfos;

    auto i = std::find_if(begin, end, pred2);

    if (i != end)
    {
        std::stringstream ss;
        ss << "The function " << i->name << " is used in conjunction with a field "
           << "that should be masked for '" << zUser << "'@'" << zHost << "', access is denied.";

        set_response(create_error_response(ss.str().c_str()));

        is_used = true;
    }

    return is_used;
}

bool MaskingFilterSession::is_variable_defined(GWBUF* pPacket, const char* zUser, const char* zHost)
{
    mxb_assert(qc_query_is_type(qc_get_type_mask(pPacket), QUERY_TYPE_USERVAR_WRITE));

    bool is_defined = false;

    SMaskingRules sRules = m_filter.rules();

    auto pred = [&sRules, zUser, zHost](const QC_FIELD_INFO& field_info) {
            bool rv = false;

            if (strcmp(field_info.column, "*") == 0)
            {
                // If "*" is used, then we must block if there is any rule for the current user.
                rv = sRules->has_rule_for(zUser, zHost);
            }
            else
            {
                rv = sRules->get_rule_for(field_info, zUser, zHost) ? true : false;
            }

            return rv;
        };

    const QC_FIELD_INFO* pInfos;
    size_t nInfos;

    qc_get_field_info(pPacket, &pInfos, &nInfos);

    const QC_FIELD_INFO* begin = pInfos;
    const QC_FIELD_INFO* end = begin + nInfos;

    auto i = std::find_if(begin, end, pred);

    if (i != end)
    {
        const char* zColumn = i->column;

        std::stringstream ss;

        if (strcmp(zColumn, "*") == 0)
        {
            ss << "'*' is used in the definition of a variable and there are masking rules "
               << "for '" << zUser << "'@'" << zHost << "', access is denied.";
        }
        else
        {
            ss << "The field " << i->column << " that should be masked for '" << zUser << "'@'" << zHost
               << "' is used when defining a variable, access is denied.";
        }

        set_response(create_error_response(ss.str().c_str()));
        is_defined = true;
    }

    return is_defined;
}

bool MaskingFilterSession::is_union_or_subquery_used(GWBUF* pPacket, const char* zUser, const char* zHost)
{
    mxb_assert(qc_get_operation(pPacket) == QUERY_OP_SELECT);

    const MaskingFilter::Config& config = m_filter.config();

    mxb_assert(config.check_unions() || config.check_subqueries());

    bool is_used = false;

    SMaskingRules sRules = m_filter.rules();

    uint32_t mask = 0;

    if (config.check_unions())
    {
        mask |= QC_FIELD_UNION;
    }

    if (config.check_subqueries())
    {
        mask |= QC_FIELD_SUBQUERY;
    }

    auto pred = [&sRules, mask, zUser, zHost](const QC_FIELD_INFO& field_info) {
            bool rv = false;

            if (field_info.context & mask)
            {
                if (strcmp(field_info.column, "*") == 0)
                {
                    // If "*" is used, then we must block if there is any rule for the current user.
                    rv = sRules->has_rule_for(zUser, zHost);
                }
                else
                {
                    rv = sRules->get_rule_for(field_info, zUser, zHost) ? true : false;
                }
            }

            return rv;
        };

    const QC_FIELD_INFO* pInfos;
    size_t nInfos;

    qc_get_field_info(pPacket, &pInfos, &nInfos);

    const QC_FIELD_INFO* begin = pInfos;
    const QC_FIELD_INFO* end = begin + nInfos;

    auto i = std::find_if(begin, end, pred);

    if (i != end)
    {
        const char* zColumn = i->column;

        std::stringstream ss;

        if (config.check_unions() && (i->context & QC_FIELD_UNION))
        {
            if (strcmp(zColumn, "*") == 0)
            {
                ss << "'*' is used in the second or subsequent SELECT of a UNION and there are "
                   << "masking rules for '" << zUser << "'@'" << zHost << "', access is denied.";
            }
            else
            {
                ss << "The field " << zColumn << " that should be masked for '" << zUser << "'@'" << zHost
                   << "' is used in the second or subsequent SELECT of a UNION, access is denied.";
            }
        }
        else if (config.check_subqueries() && (i->context & QC_FIELD_SUBQUERY))
        {
            if (strcmp(zColumn, "*") == 0)
            {
                ss << "'*' is used in a subquery and there are masking rules for '"
                   << zUser << "'@'" << zHost << "', access is denied.";
            }
            else
            {
                ss << "The field " << zColumn << " that should be masked for '"
                   << zUser << "'@'" << zHost << "' is used in a subquery, access is denied.";
            }
        }
        else
        {
            mxb_assert(!true);
        }

        set_response(create_error_response(ss.str().c_str()));
        is_used = true;
    }

    return is_used;
}

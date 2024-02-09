/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "masking"
#include "maskingfiltersession.hh"

#include <sstream>

#include <maxscale/buffer.hh>
#include <maxscale/filter.hh>
#include <maxscale/parser.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

#include "maskingfilter.hh"
#include "mysql.hh"

using maxscale::Buffer;
using maxscale::Parser;
using std::ostream;
using std::string;
using std::stringstream;

namespace
{

GWBUF create_error_response(const char* zMessage)
{
    return mariadb::create_error_packet(1, 1141, "HY000", zMessage);
}

GWBUF create_parse_error_response()
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

    EnableOption(Parser& parser, uint32_t option)
        : m_parser(parser)
        , m_option(option)
        , m_options(0)
        , m_disable(false)
    {
        if (m_option)
        {
            m_options = m_parser.get_options();

            if (!(m_options & m_option))
            {
                uint32_t options = (m_options | m_option);
                MXB_AT_DEBUG(bool rv = ) m_parser.set_options(options);
                mxb_assert(rv);
                m_disable = true;
            }
        }
    }

    ~EnableOption()
    {
        if (m_disable)
        {
            MXB_AT_DEBUG(bool rv = ) m_parser.set_options(m_options);
            mxb_assert(rv);
        }
    }

private:
    Parser&  m_parser;
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
    , m_state(IGNORING_RESPONSE)
    , m_config(pFilter->config())
    , m_bypass(!m_config.sRules->has_rule_for(pSession->user().c_str(),
                                              pSession->client_remote().c_str()))
{
}

// static
MaskingFilterSession* MaskingFilterSession::create(MXS_SESSION* pSession,
                                                   SERVICE* pService,
                                                   const MaskingFilter* pFilter)
{
    return new MaskingFilterSession(pSession, pService, pFilter);
}

bool MaskingFilterSession::check_query(const GWBUF& packet)
{
    const char* zUser = m_pSession->user().c_str();
    const char* zHost = m_pSession->client_remote().c_str();
    bool acceptable = true;

    if (Parser::type_mask_contains(parser().get_type_mask(packet), mxs::sql::TYPE_USERVAR_WRITE))
    {
        if (m_config.check_user_variables)
        {
            if (is_variable_defined(packet, zUser, zHost))
            {
                acceptable = false;
            }
        }
    }
    else
    {
        mxs::sql::OpCode op = parser().get_operation(packet);

        if (op == mxs::sql::OP_SELECT)
        {
            if (m_config.check_unions || m_config.check_subqueries)
            {
                if (is_union_or_subquery_used(packet, zUser, zHost))
                {
                    acceptable = false;
                }
            }
        }

        if (acceptable && m_config.prevent_function_usage)
        {
            // An insert like "INSERT INTO t (f) VALUES ...", where the column ("f")
            // is mentioned explicitly, will be reported to be using the column "f" in
            // conjunction with the function "=", which will cause it to be rejected if
            // "f" is a column that should be masked.
            // So, INSERTs need to be excluded from the check. That does not effectively
            // change anything, as an INSERT without named columns will not ever be
            // rejected, which would be nonsensical anyway.
            // The check cannot be limited to just SELECTs, because that would
            // allow you to probe a value e.g. using UPDATEs.
            if (op != mxs::sql::OP_INSERT)
            {
                if (is_function_used(packet, zUser, zHost))
                {
                    acceptable = false;
                }
            }
        }
    }

    return acceptable;
}

bool MaskingFilterSession::check_textual_query(const GWBUF& packet)
{
    bool rv = false;

    uint32_t option = m_config.treat_string_arg_as_field ? Parser::OPTION_STRING_ARG_AS_FIELD : 0;
    EnableOption enable(parser(), option);

    auto parse_result = parser().parse(packet, Parser::COLLECT_FIELDS | Parser::COLLECT_FUNCTIONS);
    auto op = parser().get_operation(packet);

    if (op == mxs::sql::OP_EXPLAIN)
    {
        rv = true;
    }
    else if (parse_result == Parser::Result::PARSED || !m_config.require_fully_parsed)
    {
        if (Parser::type_mask_contains(parser().get_type_mask(packet), mxs::sql::TYPE_PREPARE_NAMED_STMT))
        {
            GWBUF* pP = parser().get_preparable_stmt(packet);

            if (pP)
            {
                rv = check_textual_query(*pP);
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
            rv = check_query(packet);
        }
    }
    else
    {
        set_response(create_parse_error_response());
    }

    return rv;
}

bool MaskingFilterSession::check_binary_query(const GWBUF& packet)
{
    bool rv = false;

    uint32_t option = m_config.treat_string_arg_as_field ? Parser::OPTION_STRING_ARG_AS_FIELD : 0;
    EnableOption enable(parser(), option);

    auto parse_result = parser().parse(packet, Parser::COLLECT_FIELDS | Parser::COLLECT_FUNCTIONS);
    auto op = parser().get_operation(packet);

    if (op == mxs::sql::OP_EXPLAIN)
    {
        rv = true;
    }
    else if (parse_result == Parser::Result::PARSED || !m_config.require_fully_parsed)
    {
        rv = check_query(packet);
    }
    else
    {
        set_response(create_parse_error_response());
    }

    return rv;
}

bool MaskingFilterSession::routeQuery(GWBUF&& packet)
{
    if (m_bypass)
    {
        return FilterSession::routeQuery(std::move(packet));
    }

    ComRequest request(&packet);

    // TODO: Breaks if responses are not waited for, before the next request is sent.
    switch (request.command())
    {
    case MXS_COM_QUERY:
        m_res.reset(request.command(), m_config.sRules);

        if (m_config.is_parsing_needed())
        {
            if (check_textual_query(packet))
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
        if (m_config.is_parsing_needed())
        {
            if (check_binary_query(packet))
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
        m_res.reset(request.command(), m_config.sRules);
        m_state = EXPECTING_RESPONSE;
        break;

    default:
        m_state = IGNORING_RESPONSE;
    }

    int rv = 1;

    if (m_state != EXPECTING_NOTHING)
    {
        rv = FilterSession::routeQuery(std::move(packet));
    }

    return rv;
}

bool MaskingFilterSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    if (m_bypass)
    {
        return FilterSession::clientReply(std::move(packet), down, reply);
    }

    ComResponse response(&packet);

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
            MXB_WARNING("Received data, although expected nothing.");

        case IGNORING_RESPONSE:
            break;

        case EXPECTING_RESPONSE:
            handle_response(packet);
            break;

        case EXPECTING_FIELD:
            handle_field(packet);
            break;

        case EXPECTING_ROW:
            handle_row(packet);
            break;

        case EXPECTING_FIELD_EOF:
        case EXPECTING_ROW_EOF:
            handle_eof(packet);
            break;

        case SUPPRESSING_RESPONSE:
            break;
        }
    }

    // The state may change by the code above, so need to check it again.
    int rv;
    if (m_state != SUPPRESSING_RESPONSE)
    {
        rv = FilterSession::clientReply(std::move(packet), down, reply);
    }
    else
    {
        // TODO: The return value should mean something.
        rv = 0;
    }

    return rv;
}

void MaskingFilterSession::handle_response(GWBUF& packet)
{
    ComResponse response(&packet);

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

void MaskingFilterSession::handle_field(GWBUF& packet)
{
    ComQueryResponse::ColumnDef column_def(&packet);

    if (column_def.payload_len() >= ComPacket::MAX_PAYLOAD_LEN)     // Not particularly likely...
    {
        handle_large_payload();
    }
    else
    {
        const char* zUser = m_pSession->user().c_str();
        const char* zHost = m_pSession->client_remote().c_str();
        const MaskingRules::Rule* pRule = m_res.rules()->get_rule_for(column_def, zUser, zHost);

        if (m_res.append_type_and_rule(column_def.type(), pRule))
        {
            // All fields have been read.
            m_state = EXPECTING_FIELD_EOF;
        }
    }
}

void MaskingFilterSession::handle_eof(GWBUF& packet)
{
    ComResponse response(&packet);

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
        MXB_ERROR("Expected EOF, got something else: %d", response.type());
        m_state = IGNORING_RESPONSE;
    }
}

namespace
{

void warn_of_type_mismatch(const MaskingRules::Rule& rule)
{
    MXB_WARNING("The rule targeting \"%s\" matches a column "
                "that is not of string type.",
                rule.match().c_str());
}
}

void MaskingFilterSession::handle_row(GWBUF& packet)
{
    ComPacket response(&packet);

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
    if (m_config.large_payload == Config::LARGE_ABORT)
    {
        MXB_WARNING("Payload > 16MB, closing the connection.");
        m_pSession->kill();
        m_state = SUPPRESSING_RESPONSE;
    }
    else
    {
        MXB_WARNING("Payload > 16MB, no masking is performed.");
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
                        mxq::LEncString s = value.as_string();
                        pRule->rewrite(s);
                    }
                    else if (m_config.warn_type_mismatch == Config::WARN_ALWAYS)
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
                        mxq::LEncString s = value.as_string();
                        pRule->rewrite(s);
                    }
                    else if (m_config.warn_type_mismatch == Config::WARN_ALWAYS)
                    {
                        warn_of_type_mismatch(*pRule);
                    }
                }
                ++i;
            }
        }
        break;

    default:
        MXB_ERROR("Unexpected request: %d", m_res.command());
        mxb_assert(!true);
    }
}

bool MaskingFilterSession::is_function_used(const GWBUF& packet, const char* zUser, const char* zHost)
{
    bool is_used = false;

    auto pred1 = [this, zUser, zHost](const Parser::FieldInfo& field_info) {
        const MaskingRules::Rule* pRule = m_config.sRules->get_rule_for(field_info, zUser, zHost);

        return pRule ? true : false;
    };

    auto pred2 = [&pred1](const Parser::FunctionInfo& function_info) {
        const Parser::FieldInfo* begin = function_info.fields;
        const Parser::FieldInfo* end = begin + function_info.n_fields;

        auto i = std::find_if(begin, end, pred1);

        return i != end;
    };

    const Parser::FunctionInfo* pInfos;
    size_t nInfos;

    parser().get_function_info(packet, &pInfos, &nInfos);

    const Parser::FunctionInfo* begin = pInfos;
    const Parser::FunctionInfo* end = begin + nInfos;

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

bool MaskingFilterSession::is_variable_defined(const GWBUF& packet, const char* zUser, const char* zHost)
{
    mxb_assert(Parser::type_mask_contains(parser().get_type_mask(packet), mxs::sql::TYPE_USERVAR_WRITE));

    bool is_defined = false;

    auto pred = [this, zUser, zHost](const Parser::FieldInfo& field_info) {
        bool rv = false;

        if (field_info.column == "*")
        {
            // If "*" is used, then we must block if there is any rule for the current user.
            rv = m_config.sRules->has_rule_for(zUser, zHost);
        }
        else
        {
            rv = m_config.sRules->get_rule_for(field_info, zUser, zHost) ? true : false;
        }

        return rv;
    };

    const Parser::FieldInfo* pInfos;
    size_t nInfos;

    parser().get_field_info(packet, &pInfos, &nInfos);

    const Parser::FieldInfo* begin = pInfos;
    const Parser::FieldInfo* end = begin + nInfos;

    auto i = std::find_if(begin, end, pred);

    if (i != end)
    {
        std::stringstream ss;

        if (i->column == "*")
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

bool MaskingFilterSession::is_union_or_subquery_used(const GWBUF& packet, const char* zUser,
                                                     const char* zHost)
{
    mxb_assert(parser().get_operation(packet) == mxs::sql::OP_SELECT);
    mxb_assert(m_config.check_unions || m_config.check_subqueries);

    bool is_used = false;

    uint32_t mask = 0;

    if (m_config.check_unions)
    {
        mask |= Parser::FIELD_UNION;
    }

    if (m_config.check_subqueries)
    {
        mask |= Parser::FIELD_SUBQUERY;
    }

    auto pred = [this, mask, zUser, zHost](const Parser::FieldInfo& field_info) {
        bool rv = false;

        if (field_info.context & mask)
        {
            if (field_info.column == "*")
            {
                // If "*" is used, then we must block if there is any rule for the current user.
                rv = m_config.sRules->has_rule_for(zUser, zHost);
            }
            else
            {
                rv = m_config.sRules->get_rule_for(field_info, zUser, zHost) ? true : false;
            }
        }

        return rv;
    };

    const Parser::FieldInfo* pInfos;
    size_t nInfos;

    parser().get_field_info(packet, &pInfos, &nInfos);

    const Parser::FieldInfo* begin = pInfos;
    const Parser::FieldInfo* end = begin + nInfos;

    auto i = std::find_if(begin, end, pred);

    if (i != end)
    {
        std::string_view column = i->column;

        std::stringstream ss;

        if (m_config.check_unions && (i->context & Parser::FIELD_UNION))
        {
            if (column == "*")
            {
                ss << "'*' is used in the second or subsequent SELECT of a UNION and there are "
                   << "masking rules for '" << zUser << "'@'" << zHost << "', access is denied.";
            }
            else
            {
                ss << "The field " << column << " that should be masked for '" << zUser << "'@'" << zHost
                   << "' is used in the second or subsequent SELECT of a UNION, access is denied.";
            }
        }
        else if (m_config.check_subqueries && (i->context & Parser::FIELD_SUBQUERY))
        {
            if (column == "*")
            {
                ss << "'*' is used in a subquery and there are masking rules for '"
                   << zUser << "'@'" << zHost << "', access is denied.";
            }
            else
            {
                ss << "The field " << column << " that should be masked for '"
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

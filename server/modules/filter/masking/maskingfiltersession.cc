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

#define MXS_MODULE_NAME "masking"
#include "maskingfiltersession.hh"

#include <sstream>

#include <maxscale/buffer.hh>
#include <maxscale/filter.hh>
#include <maxscale/modutil.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/poll.h>
#include <maxscale/protocol/mysql.h>

#include "maskingfilter.hh"
#include "mysql.hh"

using maxscale::Buffer;
using std::ostream;
using std::string;
using std::stringstream;

MaskingFilterSession::MaskingFilterSession(MXS_SESSION* pSession, const MaskingFilter* pFilter)
    : maxscale::FilterSession(pSession)
    , m_filter(*pFilter)
    , m_state(IGNORING_RESPONSE)
{
}

MaskingFilterSession::~MaskingFilterSession()
{
}

//static
MaskingFilterSession* MaskingFilterSession::create(MXS_SESSION* pSession, const MaskingFilter* pFilter)
{
    return new MaskingFilterSession(pSession, pFilter);
}

int MaskingFilterSession::routeQuery(GWBUF* pPacket)
{
    ComRequest request(pPacket);

    // TODO: Breaks if responses are not waited for, before the next request is sent.
    switch (request.command())
    {
    case MXS_COM_QUERY:
        m_res.reset(request.command(), m_filter.rules());

        if (m_filter.config().prevent_function_usage() && reject_if_function_used(pPacket))
        {
            m_state = EXPECTING_NOTHING;
        }
        else
        {
            m_state = EXPECTING_RESPONSE;
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

    return rv;
}

int MaskingFilterSession::clientReply(GWBUF* pPacket)
{
    mxb_assert(GWBUF_IS_CONTIGUOUS(pPacket));

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
        rv = FilterSession::clientReply(pPacket);
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

    case ComResponse::LOCAL_INFILE_PACKET: // GET_MORE_CLIENT_DATA/SEND_MORE_CLIENT_DATA
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

    if (column_def.payload_len() >= ComPacket::MAX_PAYLOAD_LEN) // Not particularly likely...
    {
        handle_large_payload();
    }
    else
    {
        const char *zUser = session_get_user(m_pSession);
        const char *zHost = session_get_remote(m_pSession);

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
                "that is not of string type.", rule.match().c_str());
}

}

void MaskingFilterSession::handle_row(GWBUF* pPacket)
{
    ComPacket response(pPacket);

    if ((response.payload_len() == ComEOF::PAYLOAD_LEN) &&
        (ComResponse(response).type() == ComResponse::EOF_PACKET))
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
        poll_fake_hangup_event(m_pSession->client_dcb);
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

                    if (value.is_string())
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

                    if (value.is_string())
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

bool MaskingFilterSession::reject_if_function_used(GWBUF* pPacket)
{
    bool rejected = false;

    SMaskingRules sRules = m_filter.rules();

    const char *zUser = session_get_user(m_pSession);
    const char *zHost = session_get_remote(m_pSession);

    if (!zUser)
    {
        zUser = "";
    }

    if (!zHost)
    {
        zHost = "";
    }

    auto pred1 = [&sRules, zUser, zHost](const QC_FIELD_INFO& field_info)
        {
            const MaskingRules::Rule* pRule = sRules->get_rule_for(field_info, zUser, zHost);

            return pRule ? true : false;
        };

    auto pred2 = [&sRules, zUser, zHost, &pred1](const QC_FUNCTION_INFO& function_info)
        {
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

        GWBUF* pResponse = modutil_create_mysql_err_msg(1, 0, 1141, "HY000", ss.str().c_str());
        set_response(pResponse);

        rejected = true;
    }

    return rejected;
}

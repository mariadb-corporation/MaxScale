/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "rules.hh"

#include <maxscale/alloc.h>
#include <maxscale/buffer.h>
#include <maxscale/log_manager.h>
#include <maxscale/modutil.h>
#include <maxscale/protocol/mysql.h>

static inline bool query_is_sql(GWBUF* query)
{
    return modutil_is_SQL(query) || modutil_is_SQL_prepare(query);
}

Rule::Rule(std::string name):
    data(NULL),
    name(name),
    type(RT_PERMISSION),
    on_queries(FW_OP_UNDEFINED),
    times_matched(0),
    active(NULL)
{
}

Rule::~Rule()
{
}

bool Rule::matches_query(GWBUF* buffer, char** msg)
{
    *msg = create_error("Permission denied at this time.");
    MXS_NOTICE("rule '%s': query denied at this time.", name.c_str());
    return true;
}

bool Rule::need_full_parsing(GWBUF* buffer) const
{
    bool rval = false;

    if (type == RT_COLUMN ||
        type == RT_FUNCTION ||
        type == RT_USES_FUNCTION ||
        type == RT_WILDCARD ||
        type == RT_CLAUSE)
    {
        switch (qc_get_operation(buffer))
        {
        case QUERY_OP_SELECT:
        case QUERY_OP_UPDATE:
        case QUERY_OP_INSERT:
        case QUERY_OP_DELETE:
            rval = true;
            break;

        default:
            break;
        }
    }

    return rval;
}

bool Rule::matches_query_type(GWBUF* buffer)
{
    qc_query_op_t optype = qc_get_operation(buffer);

    return on_queries == FW_OP_UNDEFINED ||
           (on_queries & qc_op_to_fw_op(optype)) ||
           (MYSQL_IS_COM_INIT_DB(GWBUF_DATA(buffer)) &&
            (on_queries & FW_OP_CHANGE_DB));
}

bool WildCardRule::matches_query(GWBUF *queue, char **msg)
{
    bool rval = false;

    if (query_is_sql(queue))
    {
        const QC_FIELD_INFO* infos;
        size_t n_infos;
        qc_get_field_info(queue, &infos, &n_infos);

        for (size_t i = 0; i < n_infos; ++i)
        {
            if (strcmp(infos[i].column, "*") == 0)
            {
                MXS_NOTICE("rule '%s': query contains a wildcard.", name.c_str());
                rval = true;
                *msg = create_error("Usage of wildcard denied.");
            }
        }
    }

    return rval;
}

bool NoWhereClauseRule::matches_query(GWBUF* buffer, char** msg)
{
    bool rval = false;

    if (query_is_sql(buffer) && !qc_query_has_clause(buffer))
    {
        rval = true;
        *msg = create_error("Required WHERE/HAVING clause is missing.");
        MXS_NOTICE("rule '%s': query has no where/having "
                   "clause, query is denied.", name.c_str());
    }

    return rval;
}

bool RegexRule::matches_query(GWBUF* buffer, char** msg)
{
    bool rval = false;

    if (query_is_sql(buffer))
    {
        pcre2_code* re = m_re.get();
        pcre2_match_data *mdata = pcre2_match_data_create_from_pattern(re, NULL);
        MXS_ABORT_IF_NULL(mdata);

        char* sql;
        int len;
        modutil_extract_SQL(buffer, &sql, &len);

        if (pcre2_match(re, (PCRE2_SPTR)sql, (size_t)len, 0, 0, mdata, NULL) > 0)
        {
            MXS_NOTICE("rule '%s': regex matched on query", name.c_str());
            rval = true;
            *msg = create_error("Permission denied, query matched regular expression.");
        }

        pcre2_match_data_free(mdata);
    }

    return rval;
}

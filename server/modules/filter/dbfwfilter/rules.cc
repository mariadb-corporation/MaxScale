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

#include <algorithm>

#include <maxscale/alloc.h>
#include <maxscale/buffer.h>
#include <maxscale/log_manager.h>
#include <maxscale/modutil.h>
#include <maxscale/protocol/mysql.h>

static inline bool query_is_sql(GWBUF* query)
{
    return modutil_is_SQL(query) || modutil_is_SQL_prepare(query);
}

Rule::Rule(std::string name, std::string type):
    on_queries(FW_OP_UNDEFINED),
    times_matched(0),
    active(NULL),
    m_name(name),
    m_type(type)
{
}

Rule::~Rule()
{
}

bool Rule::matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const
{
    *msg = create_error("Permission denied at this time.");
    MXS_NOTICE("rule '%s': query denied at this time.", name().c_str());
    return true;
}

bool Rule::matches_query_type(GWBUF* buffer) const
{
    bool rval = true;

    if (on_queries != FW_OP_UNDEFINED)
    {
        rval = false;

        if (query_is_sql(buffer))
        {
            qc_query_op_t optype = qc_get_operation(buffer);

            rval = (on_queries & qc_op_to_fw_op(optype)) ||
                   (MYSQL_IS_COM_INIT_DB(GWBUF_DATA(buffer)) &&
                    (on_queries & FW_OP_CHANGE_DB));
        }
    }

    return rval;
}

const std::string& Rule::name() const
{
    return m_name;
}

const std::string& Rule::type() const
{
    return m_type;
}

bool WildCardRule::matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const
{
    bool rval = false;

    if (query_is_sql(buffer))
    {
        const QC_FIELD_INFO* infos;
        size_t n_infos;
        qc_get_field_info(buffer, &infos, &n_infos);

        for (size_t i = 0; i < n_infos; ++i)
        {
            if (strcmp(infos[i].column, "*") == 0)
            {
                MXS_NOTICE("rule '%s': query contains a wildcard.", name().c_str());
                rval = true;
                *msg = create_error("Usage of wildcard denied.");
            }
        }
    }

    return rval;
}

bool NoWhereClauseRule::matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const
{
    bool rval = false;

    if (query_is_sql(buffer) && !qc_query_has_clause(buffer))
    {
        rval = true;
        *msg = create_error("Required WHERE/HAVING clause is missing.");
        MXS_NOTICE("rule '%s': query has no where/having "
                   "clause, query is denied.", name().c_str());
    }

    return rval;
}

bool RegexRule::matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const
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
            MXS_NOTICE("rule '%s': regex matched on query", name().c_str());
            rval = true;
            *msg = create_error("Permission denied, query matched regular expression.");
        }

        pcre2_match_data_free(mdata);
    }

    return rval;
}

bool ColumnsRule::matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const
{
    bool rval = false;

    if (query_is_sql(buffer))
    {
        const QC_FIELD_INFO* infos;
        size_t n_infos;
        qc_get_field_info(buffer, &infos, &n_infos);

        for (size_t i = 0; !rval && i < n_infos; ++i)
        {
            std::string tok = infos[i].column;
            ValueList::const_iterator it = std::find(m_values.begin(), m_values.end(), tok);

            if (it != m_values.end())
            {
                MXS_NOTICE("rule '%s': query targets forbidden column: %s",
                           name().c_str(), tok.c_str());
                *msg = create_error("Permission denied to column '%s'.", tok.c_str());
                rval = true;
                break;
            }
        }
    }

    return rval;
}


bool FunctionRule::matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const
{
    bool rval = false;

    if (query_is_sql(buffer))
    {
        const QC_FUNCTION_INFO* infos;
        size_t n_infos;
        qc_get_function_info(buffer, &infos, &n_infos);

        if (n_infos == 0 && session->get_action() == FW_ACTION_ALLOW)
        {
            rval = true;
        }
        else
        {
            for (size_t i = 0; i < n_infos; ++i)
            {
                std::string tok = infos[i].name;
                ValueList::const_iterator it = std::find(m_values.begin(), m_values.end(), tok);

                if (it != m_values.end())
                {
                    MXS_NOTICE("rule '%s': query uses forbidden function: %s",
                               name().c_str(), tok.c_str());
                    *msg = create_error("Permission denied to function '%s'.", tok.c_str());
                    rval = true;
                    break;
                }

            }
        }
    }

    return rval;
}

bool FunctionUsageRule::matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const
{
    if (query_is_sql(buffer))
    {
        const QC_FUNCTION_INFO* infos;
        size_t n_infos;
        qc_get_function_info(buffer, &infos, &n_infos);

        for (size_t i = 0; i < n_infos; ++i)
        {
            for (size_t j = 0; j < infos[i].n_fields; j++)
            {
                std::string tok = infos[i].fields[j].column;
                ValueList::const_iterator it = std::find(m_values.begin(), m_values.end(), tok);

                if (it != m_values.end())
                {
                    MXS_NOTICE("rule '%s': query uses a function with forbidden column: %s",
                               name().c_str(), tok.c_str());
                    *msg = create_error("Permission denied to column '%s' with function.", tok.c_str());
                    return true;
                }
            }
        }
    }

    return false;
}

bool ColumnFunctionRule::matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const
{
    if (query_is_sql(buffer))
    {
        const QC_FUNCTION_INFO* infos;
        size_t n_infos;
        qc_get_function_info(buffer, &infos, &n_infos);

        for (size_t i = 0; i < n_infos; ++i)
        {
            ValueList::const_iterator func_it = std::find(m_values.begin(),
                                                          m_values.end(),
                                                          infos[i].name);

            if (func_it != m_values.end())
            {
                /** The function matches, now check if the column matches */

                for (size_t j = 0; j < infos[i].n_fields; j++)
                {
                    ValueList::const_iterator col_it = std::find(m_columns.begin(),
                                                                 m_columns.end(),
                                                                 infos[i].fields[j].column);

                    if (col_it != m_columns.end())
                    {
                        MXS_NOTICE("rule '%s': query uses function '%s' with forbidden column: %s",
                                   name().c_str(), func_it->c_str(), col_it->c_str());
                        *msg = create_error("Permission denied to column '%s' with function '%s'.",
                                            col_it->c_str(), func_it->c_str());
                        return true;
                    }
                }

            }
        }
    }

    return false;
}

bool LimitQueriesRule::matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const
{
    QuerySpeed* queryspeed = session->query_speed();
    time_t time_now = time(NULL);
    bool matches = false;

    if (queryspeed->active)
    {
        if (difftime(time_now, queryspeed->triggered) < m_holdoff)
        {
            double blocked_for = m_holdoff - difftime(time_now, queryspeed->triggered);
            *msg = create_error("Queries denied for %f seconds", blocked_for);
            matches = true;

            MXS_INFO("rule '%s': user denied for %f seconds",
                     name().c_str(), blocked_for);
        }
        else
        {
            queryspeed->active = false;
            queryspeed->count = 0;
        }
    }
    else
    {
        if (queryspeed->count >= m_max)
        {
            MXS_INFO("rule '%s': query limit triggered (%d queries in %d seconds), "
                     "denying queries from user for %d seconds.", name().c_str(),
                     m_max, m_timeperiod, m_holdoff);

            queryspeed->triggered = time_now;
            queryspeed->active = true;
            matches = true;

            double blocked_for = m_holdoff - difftime(time_now, queryspeed->triggered);
            *msg = create_error("Queries denied for %f seconds", blocked_for);
        }
        else if (queryspeed->count == 0)
        {
            queryspeed->first_query = time_now;
            queryspeed->count = 1;
        }
        else if (difftime(time_now, queryspeed->first_query) < m_timeperiod)
        {
            queryspeed->count++;
        }
        else
        {
            /** The time period was exceeded, reset the query count */
            queryspeed->count = 0;
        }
    }

    return matches;
}

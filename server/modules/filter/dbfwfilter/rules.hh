#pragma once
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

#include "dbfwfilter.hh"

#include <algorithm>

#include <maxscale/pcre2.hh>

namespace
{

static bool is_dml(GWBUF* buffer)
{
    qc_query_op_t optype = qc_get_operation(buffer);

    switch (optype)
    {
    case QUERY_OP_SELECT:
    case QUERY_OP_UPDATE:
    case QUERY_OP_INSERT:
    case QUERY_OP_DELETE:
        return true;

    default:
        return false;
    }
}

}

/**
 * A structure used to identify individual rules and to store their contents
 *
 * Each type of rule has different requirements that are expressed as void pointers.
 * This allows to match an arbitrary set of rules against a user.
 */
class Rule
{
    Rule(const Rule&);
    Rule& operator=(const Rule&);

public:
    Rule(std::string name, std::string type = "PERMISSION");
    virtual ~Rule();
    virtual bool matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const;

    virtual bool need_full_parsing(GWBUF* buffer) const
    {
        return false;
    }

    bool matches_query_type(GWBUF* buffer) const;
    const std::string& name() const;
    const std::string& type() const;

    uint32_t       on_queries;    /*< Types of queries to inspect */
    int            times_matched; /*< Number of times this rule has been matched */
    TIMERANGE*     active;        /*< List of times when this rule is active */

private:
    std::string    m_name;          /*< Name of the rule */
    std::string    m_type;          /*< Name of the rule */
};

/**
 * Matches if a query uses the wildcard character, `*`.
 */
class WildCardRule: public Rule
{
    WildCardRule(const WildCardRule&);
    WildCardRule& operator=(const WildCardRule&);

public:
    WildCardRule(std::string name):
        Rule(name, "WILDCARD")
    {
    }

    ~WildCardRule()
    {
    }

    bool need_full_parsing(GWBUF* buffer) const
    {
        return is_dml(buffer);
    }

    bool matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const;
};

/**
 * Matches if a query has no WHERE clause
 */
class NoWhereClauseRule: public Rule
{
    NoWhereClauseRule(const NoWhereClauseRule&);
    NoWhereClauseRule& operator=(const NoWhereClauseRule&);

public:
    NoWhereClauseRule(std::string name):
        Rule(name, "CLAUSE")
    {
    }

    ~NoWhereClauseRule()
    {
    }

    bool need_full_parsing(GWBUF* buffer) const
    {
        return is_dml(buffer);
    }

    bool matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const;

};

static void make_lower(std::string& value)
{
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
}

class ValueListRule: public Rule
{
    ValueListRule(const ValueListRule&);
    ValueListRule& operator=(const ValueListRule&);

public:
    bool need_full_parsing(GWBUF* buffer) const
    {
        return is_dml(buffer);
    }

protected:
    ValueListRule(std::string name, std::string type, const ValueList& values):
        Rule(name, type),
        m_values(values)
    {
        std::for_each(m_values.begin(), m_values.end(), make_lower);
    }

    ValueList m_values;
};

/**
 * Matches if a query uses one of the columns
 */
class ColumnsRule: public ValueListRule
{
    ColumnsRule(const ColumnsRule&);
    ColumnsRule& operator=(const ColumnsRule&);

public:
    ColumnsRule(std::string name, const ValueList& values):
        ValueListRule(name, "COLUMN", values)
    {
    }

    bool matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const;
};

/**
 * Matches if a query uses one of the functions
 */
class FunctionRule: public ValueListRule
{
    FunctionRule(const FunctionRule&);
    FunctionRule& operator=(const FunctionRule&);

public:
    FunctionRule(std::string name, const ValueList& values, bool inverted):
        ValueListRule(name, inverted ? "NOT_FUNCTION" : "FUNCTION", values),
        m_inverted(inverted)
    {
    }

    bool matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const;

private:
    bool m_inverted; /*< Should the match be inverted. */
};

/**
 * Matches if a query uses any functions
 */
class FunctionUsageRule: public ValueListRule
{
    FunctionUsageRule(const FunctionUsageRule&);
    FunctionUsageRule& operator=(const FunctionUsageRule&);

public:
    FunctionUsageRule(std::string name, const ValueList& values):
        ValueListRule(name, "FUNCTION_USAGE", values)
    {
    }

    bool matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const;
};

/**
 * Matches if a query uses a function with a specific column
 */
class ColumnFunctionRule: public ValueListRule
{
    ColumnFunctionRule(const ColumnFunctionRule&);
    ColumnFunctionRule& operator=(const ColumnFunctionRule&);

public:
    ColumnFunctionRule(std::string name, const ValueList& values, const ValueList& columns, bool inverted):
        ValueListRule(name, inverted ? "NOT_COLUMN_FUNCTION" : "COLUMN_FUNCTION", values),
        m_columns(columns),
        m_inverted(inverted)
    {
    }

    bool matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const;

private:
    ValueList m_columns;  /*< List of columns to match */
    bool      m_inverted; /*< Should the match be inverted. */
};

/**
 * Matches if a queries are executed too quickly
 */
class LimitQueriesRule: public Rule
{
    LimitQueriesRule(const LimitQueriesRule&);
    LimitQueriesRule& operator=(const LimitQueriesRule&);

public:
    LimitQueriesRule(std::string name, int max, int timeperiod, int holdoff):
        Rule(name, "THROTTLE"),
        m_max(max),
        m_timeperiod(timeperiod),
        m_holdoff(holdoff)
    {
    }

    ~LimitQueriesRule()
    {
    }

    bool need_full_parsing(GWBUF* buffer) const
    {
        return is_dml(buffer);
    }

    bool matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const;

private:
    int m_max;
    int m_timeperiod;
    int m_holdoff;
};

/**
 * Matches if a queries matches a pattern
 */
class RegexRule: public Rule
{
    RegexRule(const RegexRule&);
    RegexRule& operator=(const RegexRule&);

public:
    RegexRule(std::string name, pcre2_code* re):
        Rule(name, "REGEX"),
        m_re(re)
    {
    }

    ~RegexRule()
    {
    }

    bool need_full_parsing(GWBUF* buffer) const
    {
        return false;
    }

    bool matches_query(DbfwSession* session, GWBUF* buffer, char** msg) const;

private:
    mxs::Closer<pcre2_code*> m_re;
};

typedef std::tr1::shared_ptr<Rule> SRule;
typedef std::list<SRule>           RuleList;

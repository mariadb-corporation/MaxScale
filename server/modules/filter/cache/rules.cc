/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "cache"
#include "rules.hh"

#include <errno.h>
#include <stdio.h>
#include <new>
#include <vector>

#include <maxbase/alloc.hh>
#include <maxbase/string.hh>
#include <maxscale/config.hh>
#include <maxscale/parser.hh>
#include <maxscale/protocol/mariadb/maxscale.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/session.hh>

#include "cachefilter.hh"

using mxb::sv_case_eq;
using std::shared_ptr;
using std::unique_ptr;
using mxs::Parser;

namespace
{

const char KEY_ATTRIBUTE[] = "attribute";
const char KEY_OP[] = "op";
const char KEY_STORE[] = "store";
const char KEY_USE[] = "use";
const char KEY_VALUE[] = "value";

const char VALUE_ATTRIBUTE_COLUMN[] = "column";
const char VALUE_ATTRIBUTE_DATABASE[] = "database";
const char VALUE_ATTRIBUTE_QUERY[] = "query";
const char VALUE_ATTRIBUTE_TABLE[] = "table";
const char VALUE_ATTRIBUTE_USER[] = "user";

const char VALUE_OP_EQ[] = "=";
const char VALUE_OP_NEQ[] = "!=";
const char VALUE_OP_LIKE[] = "like";
const char VALUE_OP_UNLIKE[] = "unlike";

template<class K, class V>
std::map<V, K> invert_map(const std::map<K, V>& from)
{
    std::map<V, K> to;
    for (const auto& kv : from)
    {
        to.emplace(kv.second, kv.first);
    }

    return to;
}

struct ThisUnit
{
    ThisUnit()
        : attributes_by_name({
                {VALUE_ATTRIBUTE_COLUMN,   CacheRule::Attribute::COLUMN},
                {VALUE_ATTRIBUTE_DATABASE, CacheRule::Attribute::DATABASE},
                {VALUE_ATTRIBUTE_QUERY,    CacheRule::Attribute::QUERY},
                {VALUE_ATTRIBUTE_TABLE,    CacheRule::Attribute::TABLE},
                {VALUE_ATTRIBUTE_USER,     CacheRule::Attribute::USER}
            })
        , attributes_by_id(invert_map(attributes_by_name))
        , ops_by_name({
                {VALUE_OP_EQ,     CacheRule::Op::EQ},
                {VALUE_OP_NEQ,    CacheRule::Op::NEQ},
                {VALUE_OP_LIKE,   CacheRule::Op::LIKE},
                {VALUE_OP_UNLIKE, CacheRule::Op::UNLIKE}
            })
        , ops_by_id(invert_map(ops_by_name))
    {
    }

    const std::map<std::string_view, CacheRule::Attribute> attributes_by_name;
    const std::map<CacheRule::Attribute, std::string_view> attributes_by_id;

    const std::map<std::string_view, CacheRule::Op> ops_by_name;
    const std::map<CacheRule::Op, std::string_view> ops_by_id;
} this_unit;

template<class V>
const char* value_to_string(const std::map<V, std::string_view>& values_by_id, V value)
{
    auto it = values_by_id.find(value);

    if (it != values_by_id.end())
    {
        return it->second.data();
    }
    else
    {
        mxb_assert(!true);
        return "<invalid>";
    }
}

template<class V>
bool value_from_string(const std::map<std::string_view, V>& values_by_name, const char* z, V* pValue)
{
    auto it = values_by_name.find(z);
    auto end = values_by_name.end();

    if (it != end)
    {
        *pValue = it->second;
    }

    return it != end;
}

}


//
// CacheRule
//

CacheRule::~CacheRule()
{
}

//static
const char* CacheRule::to_string(CacheRule::Attribute attribute)
{
    return value_to_string(this_unit.attributes_by_id, attribute);
}

//static
bool CacheRule::from_string(const char* z, CacheRule::Attribute* pAttribute)
{
    return value_from_string(this_unit.attributes_by_name, z, pAttribute);
}

//static
const char* CacheRule::to_string(CacheRule::Op op)
{
    return value_to_string(this_unit.ops_by_id, op);
}

//static
bool CacheRule::from_string(const char* z, CacheRule::Op* pOp)
{
    return value_from_string(this_unit.ops_by_name, z, pOp);
}

bool CacheRule::eq(const CacheRuleConcrete& other) const
{
    return false;
}

bool CacheRule::eq(const CacheRuleUser& other) const
{
    return false;
}

//
// CacheRuleConcrete
//

bool CacheRuleConcrete::compare(const std::string_view& value) const
{
    bool rv;

    if (!value.empty())
    {
        rv = compare_n(value.data(), value.length());
    }
    else
    {
        if ((m_op == Op::EQ) || (m_op == Op::LIKE))
        {
            rv = false;
        }
        else
        {
            rv = true;
        }
    }

    return rv;
}

bool CacheRuleConcrete::eq(const CacheRule& other) const
{
    return other.eq(*this);
}

bool CacheRuleConcrete::eq(const CacheRuleConcrete& other) const
{
    return
        m_attribute == other.m_attribute
        && m_op == other.m_op
        && m_value == other.m_value;
}

//
// CacheRuleValue
//

bool CacheRuleValue::matches(const mxs::Parser& parser,
                             const char* zDefault_db,
                             const GWBUF* pQuery) const
{
    bool matches = false;

    switch (m_attribute)
    {
    case CacheRule::Attribute::COLUMN:
        matches = matches_column(parser, zDefault_db, pQuery);
        break;

    case CacheRule::Attribute::DATABASE:
        matches = matches_database(parser, zDefault_db, pQuery);
        break;

    case CacheRule::Attribute::TABLE:
        matches = matches_table(parser, zDefault_db, pQuery);
        break;

    case CacheRule::Attribute::QUERY:
        matches = matches_query(zDefault_db, pQuery);
        break;

    case CacheRule::Attribute::USER:
        mxb_assert(!true);
        break;

    default:
        mxb_assert(!true);
    }

    auto debug = m_config.debug;
    if ((matches && (debug & CACHE_DEBUG_MATCHING))
        || (!matches && (debug & CACHE_DEBUG_NON_MATCHING)))
    {
        std::string_view sql = mariadb::get_sql(*pQuery);

        const char* zText;

        if (matches)
        {
            zText = "MATCHES";
        }
        else
        {
            zText = "does NOT match";
        }

        MXB_NOTICE("Rule { \"attribute\": \"%s\", \"op\": \"%s\", \"value\": \"%s\" } %s \"%.*s\".",
                   to_string(m_attribute),
                   to_string(m_op),
                   m_value.c_str(),
                   zText,
                   (int)sql.length(),
                   sql.data());
    }

    return matches;
}

bool CacheRuleValue::matches_column(const mxs::Parser& parser,
                                    const char* zDefault_db,
                                    const GWBUF* pQuery) const
{
    mxb_assert(!true);
    return false;
}

bool CacheRuleValue::matches_table(const mxs::Parser& parser,
                                   const char* zDefault_db,
                                   const GWBUF* pQuery) const
{
    mxb_assert(!true);
    return false;
}

bool CacheRuleValue::matches_database(const mxs::Parser& parser,
                                      const char* zDefault_db,
                                      const GWBUF* pQuery) const
{
    mxb_assert(m_attribute == Attribute::DATABASE);

    // This works both for OP_[EQ|NEQ] and OP_[LIKE|UNLIKE], as m_value will contain what
    // needs to be matched against. In the former case, this class will be a CacheRuleCTD
    // and in the latter a CacheRuleRegex, which means that compare() below will do the
    // right thing.

    bool matches = false;
    bool fullnames = true;

    // TODO: Make qc const-correct.
    for (const auto& name : parser.get_table_names(const_cast<GWBUF&>(*pQuery)))
    {
        if (!name.db.empty())
        {
            matches = compare(name.db);
        }
        else
        {
            matches = compare(zDefault_db ? zDefault_db : "");
        }

        if (matches)
        {
            break;
        }
    }

    return matches;
}

bool CacheRuleValue::matches_query(const char* zDefault_db, const GWBUF* pQuery) const
{
    mxb_assert(m_attribute == Attribute::QUERY);

    // This works both for OP_[EQ|NEQ] and OP_[LIKE|UNLIKE], as m_value will contain what
    // needs to be matched against. In the former case, this class will be a CacheRuleQuery
    // and in the latter a CacheRuleRegex, which means that compare() below will do the
    // right thing.

    // Will succeed, query contains a COM_QUERY.
    std::string_view sv = mariadb::get_sql(*pQuery);
    const char* sql = sv.data();
    int len = sv.length();

    return compare_n(sql, len);
}


//
// CacheRuleSimple
//

bool CacheRuleSimple::compare_n(const char* pValue, size_t length) const
{
    return compare_n(m_value, m_op, pValue, length);
}

//static
bool CacheRuleSimple::compare_n(const std::string& lhs,
                                Op op,
                                const char* pValue, size_t length)
{
    bool compares = (strncmp(lhs.c_str(), pValue, length) == 0);

    if (op == Op::NEQ)
    {
        compares = !compares;
    }

    return compares;
}


//
// CacheRuleCTD
//

//static
CacheRuleCTD* CacheRuleCTD::create(const CacheConfig* pConfig,
                                   Attribute attribute,
                                   Op op,
                                   const char* zValue)
{
    mxb_assert((attribute == Attribute::COLUMN)
               || (attribute == Attribute::TABLE)
               || (attribute == Attribute::DATABASE));
    mxb_assert((op == Op::EQ) || (op == Op::NEQ));

    CacheRuleCTD* pRule = new CacheRuleCTD(pConfig, attribute, op, zValue);

    bool error = false;

    char buffer[strlen(zValue) + 1];
    strcpy(buffer, zValue);

    const char* zFirst = nullptr;
    const char* zSecond = nullptr;
    const char* zThird = nullptr;
    char* zDot1 = strchr(buffer, '.');
    char* zDot2 = zDot1 ? strchr(zDot1 + 1, '.') : nullptr;

    if (zDot1 && zDot2)
    {
        zFirst = buffer;
        *zDot1 = 0;
        zSecond = zDot1 + 1;
        *zDot2 = 0;
        zThird = zDot2 + 1;
    }
    else if (zDot1)
    {
        zFirst = buffer;
        *zDot1 = 0;
        zSecond = zDot1 + 1;
    }
    else
    {
        zFirst = buffer;
    }

    switch (attribute)
    {
    case Attribute::COLUMN:
        {
            if (zThird)      // implies also 'first' and 'second'
            {
                pRule->m_column = zThird;
                pRule->m_table = zSecond;
                pRule->m_database = zFirst;
            }
            else if (zSecond)    // implies also 'first'
            {
                pRule->m_column = zSecond;
                pRule->m_table = zFirst;
            }
            else    // only 'zFirst'
            {
                pRule->m_column = zFirst;
            }
        }
        break;

    case Attribute::TABLE:
        if (zThird)
        {
            MXB_ERROR("A cache rule value for matching a table name, cannot contain two dots: '%s'",
                      zValue);
            error = true;
        }
        else
        {
            if (zSecond)     // implies also 'zFirst'
            {
                pRule->m_database = zFirst;
                pRule->m_table = zSecond;
            }
            else    // only 'zFirst'
            {
                pRule->m_table = zFirst;
            }
        }
        break;

    case Attribute::DATABASE:
        if (zSecond)
        {
            MXB_ERROR("A cache rule value for matching a database, cannot contain a dot: '%s'",
                      zValue);
            error = true;
        }
        else
        {
            pRule->m_database = zFirst;
        }
        break;

    default:
        mxb_assert(!true);
    }

    if (error)
    {
        delete pRule;
        pRule = nullptr;
    }

    return pRule;
}

bool CacheRuleCTD::matches_column(const mxs::Parser& parser,
                                  const char* zDefault_db,
                                  const GWBUF* pQuery) const
{
    mxb_assert(m_attribute == Attribute::COLUMN);
    mxb_assert((m_op == Op::EQ) || (m_op == Op::NEQ));
    mxb_assert(!m_column.empty());

    const char* zRule_column = m_column.c_str();
    const char* zRule_table = m_table.empty() ? nullptr : m_table.c_str();
    const char* zRule_database = m_database.empty() ? nullptr : m_database.c_str();

    std::string_view default_database;

    auto databases = parser.get_database_names(const_cast<GWBUF&>(*pQuery));

    if (databases.empty())
    {
        // If no databases have been mentioned, then we can assume that all
        // tables and columns that are not explicitly qualified refer to the
        // default database.
        if (zDefault_db)
        {
            default_database = zDefault_db;
        }
    }
    else if ((zDefault_db == nullptr) && (databases.size() == 1))
    {
        // If there is no default database and exactly one database has been
        // explicitly mentioned, then we can assume all tables and columns that
        // are not explicitly qualified refer to that database.
        default_database = databases[0];
    }

    auto tables = parser.get_table_names(const_cast<GWBUF&>(*pQuery));

    std::string_view default_table;

    if (tables.size() == 1)
    {
        // Only if we have exactly one table can we assume anything
        // about a table that has not been mentioned explicitly.
        default_table = tables[0].table;
    }

    const Parser::FieldInfo* infos;
    size_t n_infos;

    parser.get_field_info(const_cast<GWBUF&>(*pQuery), &infos, &n_infos);

    bool matches = false;

    size_t i = 0;
    while (!matches && (i < n_infos))
    {
        const Parser::FieldInfo* info = (infos + i);

        if (sv_case_eq(info->column, zRule_column) || strcmp(zRule_column, "*") == 0)
        {
            if (zRule_table)
            {
                std::string_view check_table = !info->table.empty() ? info->table : default_table;

                if (!check_table.empty())
                {
                    if (sv_case_eq(check_table, zRule_table))
                    {
                        if (zRule_database)
                        {
                            std::string_view check_database =
                                !info->database.empty() ? info->database : default_database;

                            if (!check_database.empty())
                            {
                                if (sv_case_eq(check_database, zRule_database))
                                {
                                    // The column, table and database matched.
                                    matches = true;
                                }
                                else
                                {
                                    // The column, table matched but the database did not.
                                    matches = false;
                                }
                            }
                            else
                            {
                                // If the rules specify a database but we do not know the database,
                                // we consider the databases not to match.
                                matches = false;
                            }
                        }
                        else
                        {
                            // If the rule specifies no database, then if the column and the table
                            // matches, the rule matches.
                            matches = true;
                        }
                    }
                    else
                    {
                        // The column matched, but the table did not.
                        matches = false;
                    }
                }
                else
                {
                    // If the rules specify a table but we do not know the table, we
                    // consider the tables not to match.
                    matches = false;
                }
            }
            else
            {
                // The column matched and there is no table rule.
                matches = true;
            }
        }
        else
        {
            // The column did not match.
            matches = false;
        }

        if (m_op == Op::NEQ)
        {
            matches = !matches;
        }

        ++i;
    }

    return matches;
}

bool CacheRuleCTD::matches_table(const mxs::Parser& parser,
                                 const char* zDefault_db,
                                 const GWBUF* pQuery) const
{
    mxb_assert(m_attribute == Attribute::TABLE);
    mxb_assert((m_op == Op::EQ) || (m_op == Op::NEQ));

    bool matches = false;
    bool fullnames = !m_database.empty();

    for (const auto& name : parser.get_table_names(const_cast<GWBUF&>(*pQuery)))
    {
        std::string_view database;
        std::string_view table;

        if (fullnames)
        {
            if (!name.db.empty())
            {
                database = name.db;
                table = name.table;
            }
            else
            {
                database = zDefault_db;
                table = name.table;
            }

            if (!database.empty())
            {
                matches = sv_case_eq(m_database, database) && sv_case_eq(m_table, table);
            }
        }
        else
        {
            matches = sv_case_eq(m_table, name.table);
        }

        if (m_op == Op::NEQ)
        {
            matches = !matches;
        }

        if (matches)
        {
            break;
        }
    }

    return matches;
}


//
// CacheRuleQuery
//

//static
CacheRuleQuery* CacheRuleQuery::create(const CacheConfig* pConfig,
                                       Attribute attribute,
                                       Op op,
                                       const char* zValue)
{
    mxb_assert(attribute == Attribute::QUERY);
    mxb_assert((op == Op::EQ) || (op == Op::NEQ));

    CacheRuleQuery* pRule = new CacheRuleQuery(pConfig, attribute, op, zValue);

    return pRule;
}


//
// CacheRuleRegex
//

//static
CacheRuleRegex* CacheRuleRegex::create(const CacheConfig* pConfig,
                                       Attribute attribute,
                                       Op op,
                                       const char* zValue)
{
    mxb_assert((op == Op::LIKE) || (op == Op::UNLIKE));

    CacheRuleRegex* pRule = nullptr;

    int errcode;
    PCRE2_SIZE erroffset;
    pcre2_code* pCode = pcre2_compile((PCRE2_SPTR)zValue,
                                      PCRE2_ZERO_TERMINATED,
                                      0,
                                      &errcode,
                                      &erroffset,
                                      nullptr);

    if (pCode)
    {
        // We do not care about the result. If JIT is not present, we have
        // complained about it already.
        pcre2_jit_compile(pCode, PCRE2_JIT_COMPLETE);

        pRule = new CacheRuleRegex(pConfig, attribute, op, zValue);
        pRule->m_pCode = pCode;
    }
    else
    {
        PCRE2_UCHAR errbuf[512];
        pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
        MXB_ERROR("Regex compilation failed at %d for regex '%s': %s",
                  (int)erroffset,
                  zValue,
                  errbuf);
    }

    return pRule;
}

CacheRuleRegex::~CacheRuleRegex()
{
    pcre2_code_free(m_pCode);
    m_pCode = nullptr;
}

bool CacheRuleRegex::compare_n(const char* zValue, size_t length) const
{
    pcre2_match_data* pData = pcre2_match_data_create_from_pattern(m_pCode, nullptr);

    bool compares = (pcre2_match(m_pCode,
                                 (PCRE2_SPTR)zValue,
                                 length,
                                 0,
                                 0,
                                 pData,
                                 nullptr) >= 0);
    pcre2_match_data_free(pData);

    if (m_op == Op::UNLIKE)
    {
        compares = !compares;
    }

    return compares;
}

bool CacheRuleRegex::matches_column(const mxs::Parser& parser,
                                    const char* zDefault_db,
                                    const GWBUF* pQuery) const
{
    mxb_assert(m_attribute == Attribute::COLUMN);
    mxb_assert((m_op == Op::LIKE) || (m_op == Op::UNLIKE));

    std::string_view default_database;

    int n_databases;
    auto databases = parser.get_database_names(const_cast<GWBUF&>(*pQuery));

    if (databases.empty())
    {
        // If no databases have been mentioned, then we can assume that all
        // tables and columns that are not explicitly qualified refer to the
        // default database.
        if (zDefault_db)
        {
            default_database = zDefault_db;
        }
    }
    else if ((zDefault_db == nullptr) && (databases.size() == 1))
    {
        // If there is no default database and exactly one database has been
        // explicitly mentioned, then we can assume all tables and columns that
        // are not explicitly qualified refer to that database.
        default_database = databases[0];
    }

    size_t default_database_len = default_database.length();

    auto tables = parser.get_table_names(const_cast<GWBUF&>(*pQuery));

    std::string_view default_table;

    if (tables.size() == 1)
    {
        // Only if we have exactly one table can we assume anything
        // about a table that has not been mentioned explicitly.
        default_table = tables[0].table;
    }

    size_t default_table_len = default_table.length();

    const Parser::FieldInfo* infos;
    size_t n_infos;

    parser.get_field_info(const_cast<GWBUF&>(*pQuery), &infos, &n_infos);

    bool matches = false;

    size_t i = 0;
    while (!matches && (i < n_infos))
    {
        const Parser::FieldInfo* info = (infos + i);

        std::string_view database;
        size_t database_len;

        if (!info->database.empty())
        {
            database = info->database;
            database_len = database.length();
        }
        else
        {
            database = default_database;
            database_len = default_database_len;
        }

        size_t table_len;
        std::string_view table;

        if (!info->table.empty())
        {
            table = info->table;
            table_len = table.length();
        }
        else
        {
            table = default_table;
            table_len = default_table_len;
        }

        char buffer[database_len + 1 + table_len + 1 + info->column.length() + 1];
        buffer[0] = 0;

        if (!database.empty())
        {
            strncat(buffer, database.data(), database.length());
            strcat(buffer, ".");
        }

        if (!table.empty())
        {
            strncat(buffer, table.data(), table.length());
            strcat(buffer, ".");
        }

        strncat(buffer, info->column.data(), info->column.length());

        matches = compare(buffer);

        ++i;
    }

    return matches;
}

bool CacheRuleRegex::matches_table(const mxs::Parser& parser,
                                   const char* zDefault_db,
                                   const GWBUF* pQuery) const
{
    mxb_assert(m_attribute == Attribute::TABLE);
    mxb_assert((m_op == Op::LIKE) || (m_op == Op::UNLIKE));

    bool matches = false;

    auto names = parser.get_table_names(const_cast<GWBUF&>(*pQuery));

    if (!names.empty())
    {
        std::string db = zDefault_db ? zDefault_db : "";

        for (const auto& name : names)
        {
            if (name.db.empty())
            {
                // Only "tbl"

                if (zDefault_db)
                {
                    matches = compare(db + '.' + std::string(name.table));
                }
                else
                {
                    matches = compare(name.table);
                }
            }
            else
            {
                // A qualified name "db.tbl".
                std::string qname(name.db);
                qname += '.';
                qname += name.table;
                matches = compare(qname);
            }

            if (matches)
            {
                break;
            }
        }
    }
    else if (m_op == Op::UNLIKE)
    {
        matches = true;
    }

    return matches;
}


//
// CacheRuleUser
//

//static
CacheRuleUser* CacheRuleUser::create(const CacheConfig* pConfig,
                                     Attribute attribute,
                                     Op op,
                                     const char* zValue)
{
    CacheRule* pDelegate = nullptr;

    mxb_assert(attribute == Attribute::USER);
    mxb_assert((op == Op::EQ) || (op == Op::NEQ));

    bool error = false;
    size_t len = strlen(zValue);

    char value[strlen(zValue) + 1];
    strcpy(value, zValue);

    char* at = strchr(value, '@');
    char* user = value;
    char* host;
    char any[2];    // sizeof("%")

    if (at)
    {
        *at = 0;
        host = at + 1;
    }
    else
    {
        strcpy(any, "%");
        host = any;
    }

    if (mariadb::trim_quotes(user))
    {
        char pcre_user[2 * len + 1];    // Surely enough

        if (*user == 0)
        {
            strcpy(pcre_user, ".*");
        }
        else
        {
            mxs_mysql_name_to_pcre(pcre_user, user, MXS_PCRE_QUOTE_VERBATIM);
        }

        if (mariadb::trim_quotes(host))
        {
            char pcre_host[2 * len + 1];    // Surely enough

            mxs_mysql_name_kind_t rv = mxs_mysql_name_to_pcre(pcre_host, host, MXS_PCRE_QUOTE_WILDCARD);

            if (rv == MXS_MYSQL_NAME_WITH_WILDCARD)
            {
                op = (op == Op::EQ ? Op::LIKE : Op::UNLIKE);

                char regexp[strlen(pcre_user) + 1 + strlen(pcre_host) + 1];

                sprintf(regexp, "%s@%s", pcre_user, pcre_host);

                pDelegate = CacheRuleRegex::create(pConfig, attribute, op, regexp);
            }
            else
            {
                // No wildcard, no need to use regexp.

                std::string value = user;
                value += "@";
                value += host;

                class RuleSimpleUser : public CacheRuleConcrete
                {
                public:
                    RuleSimpleUser(const CacheConfig* pConfig,
                                   Attribute attribute,
                                   Op op,
                                   std::string value)
                        : CacheRuleConcrete(pConfig, attribute, op, value)
                    {
                    }

                protected:
                    bool compare_n(const char* zValue, size_t length) const override
                    {
                        return CacheRuleSimple::compare_n(m_value, m_op, zValue, length);
                    }
                };

                pDelegate = new RuleSimpleUser(pConfig, attribute, op, std::move(value));
            }
        }
        else
        {
            MXB_ERROR("Could not trim quotes from host %s.", zValue);
        }
    }
    else
    {
        MXB_ERROR("Could not trim quotes from user %s.", zValue);
    }

    CacheRuleUser* pRule = nullptr;

    if (pDelegate)
    {
        unique_ptr<CacheRule> sDelegate(pDelegate);

        pRule = new CacheRuleUser(std::move(sDelegate));
    }

    return pRule;
}

bool CacheRuleUser::compare(const std::string_view& value) const
{
    return m_sDelegate->compare(value);
}

bool CacheRuleUser::compare_n(const char* value, size_t length) const
{
    return m_sDelegate->compare_n(value, length);
}

bool CacheRuleUser::matches_user(const char* account) const
{
    mxb_assert(attribute() == Attribute::USER);

    bool matches = compare(account);
    auto d = debug();

    if ((matches && (d & CACHE_DEBUG_MATCHING))
        || (!matches && (d & CACHE_DEBUG_NON_MATCHING)))
    {
        const char* text;
        if (matches)
        {
            text = "MATCHES";
        }
        else
        {
            text = "does NOT match";
        }

        MXB_NOTICE("Rule { \"attribute\": \"%s\", \"op\": \"%s\", \"value\": \"%s\" } %s \"%s\".",
                   to_string(attribute()),
                   to_string(op()),
                   value().c_str(),
                   text,
                   account);
    }

    return matches;
}

bool CacheRuleUser::eq(const CacheRule& other) const
{
    return other.eq(*this);
}

bool CacheRuleUser::eq(const CacheRuleUser& other) const
{
    return m_sDelegate->eq(*other.m_sDelegate.get());
}

//
// CacheRules
//

//static
CacheRules::Attributes CacheRules::s_store_attributes =
{
    CacheRule::Attribute::COLUMN,
    CacheRule::Attribute::DATABASE,
    CacheRule::Attribute::QUERY,
    CacheRule::Attribute::TABLE
};

//static
CacheRules::Attributes CacheRules::s_use_attributes =
{
    CacheRule::Attribute::USER
};

// static
CacheRules::SVector CacheRules::get(const CacheConfig* pConfig, const std::string& path)
{
    CacheRules::SVector sRules;

    if (!path.empty())
    {
        sRules = load(pConfig, path);
    }
    else
    {
        sRules.reset(new CacheRules::Vector);
        sRules->push_back(shared_ptr<CacheRules>(CacheRules::create(pConfig).release()));
    }

    return sRules;
}

// static
CacheRules::SVector CacheRules::load(const CacheConfig* pConfig, const char* zPath)
{
    CacheRules::SVector sRules;

    FILE* pF = fopen(zPath, "r");

    if (pF)
    {
        json_error_t error;
        json_t* pRoot = json_loadf(pF, JSON_DISABLE_EOF_CHECK, &error);

        if (pRoot)
        {
            sRules = create_all_from_json(pConfig, pRoot);

            if (!sRules)
            {
                json_decref(pRoot);
            }
        }
        else
        {
            MXB_ERROR("Loading rules file failed: (%s:%d:%d): %s",
                      zPath,
                      error.line,
                      error.column,
                      error.text);
        }

        fclose(pF);
    }
    else
    {
        MXB_ERROR("Could not open rules file %s for reading: %s",
                  zPath,
                  mxb_strerror(errno));
    }

    return sRules;
}

//static
CacheRules::SVector CacheRules::parse(const CacheConfig* pConfig, const char* zJson)
{
    CacheRules::SVector sRules;

    json_error_t error;
    json_t* pRoot = json_loads(zJson, JSON_DISABLE_EOF_CHECK, &error);

    if (pRoot)
    {
        sRules = create_all_from_json(pConfig, pRoot);

        if (!sRules)
        {
            json_decref(pRoot);
        }
    }
    else
    {
        MXB_ERROR("Parsing rules failed: (%d:%d): %s",
                  error.line,
                  error.column,
                  error.text);
    }

    return sRules;
}

bool CacheRules::should_store(const mxs::Parser& parser, const char* zDefault_db, const GWBUF* pQuery) const
{
    bool should_store = false;

    if (!m_store_rules.empty())
    {
        for (const auto& sRule : m_store_rules)
        {
            should_store = sRule->matches(parser, zDefault_db, pQuery);

            if (should_store)
            {
                break;
            }
        }
    }
    else
    {
        should_store = true;
    }

    return should_store;
}

bool CacheRules::should_use(const MXS_SESSION* session) const
{
    bool should_use = false;

    if (!m_use_rules.empty())
    {
        const auto& user = session->user();
        const auto& host = session->client_remote();

        char account[user.length() + 1 + host.length() + 1];
        sprintf(account, "%s@%s", user.c_str(), host.c_str());

        for (const auto& sRule : m_use_rules)
        {
            should_use = sRule->matches_user(account);

            if (should_use)
            {
                break;
            }
        }
    }
    else
    {
        should_use = true;
    }

    return should_use;
}

bool CacheRules::eq(const CacheRules& other) const
{
    bool rv = false;

    if (m_store_rules.size() == other.m_store_rules.size()
        && m_use_rules.size() == other.m_use_rules.size())
    {
        rv = std::equal(m_store_rules.begin(), m_store_rules.end(), other.m_store_rules.begin(),
                        [](const unique_ptr<CacheRuleValue>& sLhs,
                           const unique_ptr<CacheRuleValue>& sRhs) {
                            return *sLhs == *sRhs;
                        });

        if (rv)
        {
            rv = std::equal(m_use_rules.begin(), m_use_rules.end(), other.m_use_rules.begin(),
                            [](const unique_ptr<CacheRuleUser>& sLhs,
                               const unique_ptr<CacheRuleUser>& sRhs) {
                                return *sLhs == *sRhs;
                            });
        }
    }

    return rv;
}

// static
bool CacheRules::eq(const CacheRules::Vector& lhs, const CacheRules::Vector& rhs)
{
    bool rv = (lhs.size() == rhs.size());

    if (rv)
    {
        rv = std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                        [](const shared_ptr<CacheRules>& sLhs, const shared_ptr<CacheRules>& sRhs) {
                            return *sLhs == *sRhs;
                        });
    }

    return rv;
}

// static
bool CacheRules::eq(const CacheRules::SVector& sLhs, const CacheRules::SVector& sRhs)
{
    bool rv = false;

    if (sLhs && sRhs)
    {
        rv = eq(*sLhs.get(), *sRhs.get());
    }
    else if (!sLhs && !sRhs)
    {
        rv = true;
    }

    return rv;
}


CacheRules::CacheRules(const CacheConfig* pConfig)
    : m_config(*pConfig)
{
}

CacheRules::~CacheRules()
{
    if (m_pRoot)
    {
        json_decref(m_pRoot);
    }
}

// static
unique_ptr<CacheRules> CacheRules::create(const CacheConfig* pConfig)
{
    unique_ptr<CacheRules> sThis;

    sThis = unique_ptr<CacheRules>(new(std::nothrow) CacheRules(pConfig));

    return sThis;
}

const json_t* CacheRules::json() const
{
    return m_pRoot;
}

//static
CacheRule* CacheRules::create_simple_rule(const CacheConfig* pConfig,
                                          CacheRule::Attribute attribute,
                                          CacheRule::Op op,
                                          const char* zValue)
{
    mxb_assert((op == CacheRule::Op::EQ) || (op == CacheRule::Op::NEQ));

    CacheRule* pRule = nullptr;

    switch (attribute)
    {
    case CacheRule::Attribute::COLUMN:
    case CacheRule::Attribute::TABLE:
    case CacheRule::Attribute::DATABASE:
        pRule = CacheRuleCTD::create(pConfig, attribute, op, zValue);
        break;

    case CacheRule::Attribute::USER:
        pRule = CacheRuleUser::create(pConfig, attribute, op, zValue);
        break;

    case CacheRule::Attribute::QUERY:
        pRule = CacheRuleQuery::create(pConfig, attribute, op, zValue);
        break;

    default:
        MXB_ERROR("Unknown attribute type: %d", (int)attribute);
        mxb_assert(!true);
    }

    return pRule;
}

//static
CacheRule* CacheRules::create_rule(const CacheConfig* pConfig,
                                   CacheRule::Attribute attribute,
                                   CacheRule::Op op,
                                   const char* zValue)
{
    CacheRule* pRule = nullptr;

    switch (op)
    {
    case CacheRule::Op::EQ:
    case CacheRule::Op::NEQ:
        pRule = create_simple_rule(pConfig, attribute, op, zValue);
        break;

    case CacheRule::Op::LIKE:
    case CacheRule::Op::UNLIKE:
        pRule = CacheRuleRegex::create(pConfig, attribute, op, zValue);
        break;

    default:
        mxb_assert(!true);
        MXB_ERROR("Internal error.");
        break;
    }

    return pRule;
}

//static
CacheRules* CacheRules::create_one_from_json(const CacheConfig* pConfig, json_t* pRoot)
{
    mxb_assert(pRoot);

    CacheRules* pRules = new CacheRules(pConfig);

    if (pRules->parse_json(pRoot))
    {
        pRules->m_pRoot = pRoot;
    }
    else
    {
        delete pRules;
        pRules = nullptr;
    }

    return pRules;
}

//static
CacheRules::SVector CacheRules::create_all_from_json(const CacheConfig* pConfig, json_t* pRoot)
{
    CacheRules::SVector sRules_vector(new CacheRules::Vector);

    if (json_is_array(pRoot))
    {
        int32_t nRules = json_array_size(pRoot);

        int i;
        for (i = 0; i < nRules; ++i)
        {
            json_t* pObject = json_array_get(pRoot, i);
            mxb_assert(pObject);

            CacheRules* pRules = create_one_from_json(pConfig, pObject);

            if (pRules)
            {
                sRules_vector->push_back(shared_ptr<CacheRules>(pRules));
                // The array element reference was borrowed, so now that we
                // know a rule could be created, we must increase the reference
                // count. Otherwise bad things will happen when the reference of
                // the root object is decreased.
                json_incref(pObject);
            }
            else
            {
                break;
            }
        }

        if (i == nRules)
        {
            // We only store the objects in the array, so now we must get rid
            // of the array so that it does not leak.
            json_decref(pRoot);
        }
        else
        {
            // Ok, so something went astray.
            sRules_vector.reset();
        }
    }
    else
    {
        CacheRules* pRules = create_one_from_json(pConfig, pRoot);

        if (pRules)
        {
            sRules_vector->push_back(shared_ptr<CacheRules>(pRules));
        }
        else
        {
            sRules_vector.reset();
        }
    }

    return sRules_vector;
}

bool CacheRules::parse_json(json_t* pRoot)
{
    bool parsed = false;
    json_t* pStore = json_object_get(pRoot, KEY_STORE);

    if (pStore)
    {
        if (json_is_array(pStore))
        {
            parsed = parse_array(pStore, KEY_STORE, &CacheRules::parse_store_element);
        }
        else
        {
            MXB_ERROR("The cache rules object contains a `%s` key, but it is not an array.", KEY_STORE);
        }
    }

    if (!pStore || parsed)
    {
        json_t* pUse = json_object_get(pRoot, KEY_USE);

        if (pUse)
        {
            if (json_is_array(pUse))
            {
                parsed = parse_array(pUse, KEY_USE, &CacheRules::parse_use_element);
            }
            else
            {
                MXB_ERROR("The cache rules object contains a `%s` key, but it is not an array.", KEY_USE);
            }
        }
        else
        {
            parsed = true;
        }
    }

    return parsed;
}

bool CacheRules::parse_array(json_t* pStore,
                             const char* zName,
                             CacheRules::ElementParser parse_element)
{
    mxb_assert(json_is_array(pStore));

    bool parsed = true;

    size_t n = json_array_size(pStore);
    size_t i = 0;

    while (parsed && (i < n))
    {
        json_t* pElement = json_array_get(pStore, i);
        mxb_assert(pElement);

        if (json_is_object(pElement))
        {
            parsed = (this->*parse_element)(pElement, i);
        }
        else
        {
            MXB_ERROR("Element %lu of the '%s' array is not an object.", i, zName);
            parsed = false;
        }

        ++i;
    }

    return parsed;
}

CacheRule* CacheRules::parse_element(json_t* pObject,
                                     const char* zArray_name,
                                     size_t index,
                                     const Attributes& valid_attributes)
{
    mxb_assert(json_is_object(pObject));

    CacheRule* pRule = nullptr;

    json_t* pA = json_object_get(pObject, KEY_ATTRIBUTE);
    json_t* pO = json_object_get(pObject, KEY_OP);
    json_t* pV = json_object_get(pObject, KEY_VALUE);

    if (pA && pO && pV && json_is_string(pA) && json_is_string(pO) && json_is_string(pV))
    {
        CacheRule::Attribute attribute;

        if (get_attribute(valid_attributes, json_string_value(pA), &attribute))
        {
            CacheRule::Op op;

            if (CacheRule::from_string(json_string_value(pO), &op))
            {
                pRule = create_rule(&m_config, attribute, op, json_string_value(pV));
            }
            else
            {
                MXB_ERROR("Element %lu in the `%s` array has an invalid value "
                          "\"%s\" for 'op'.",
                          index,
                          zArray_name,
                          json_string_value(pO));
            }
        }
        else
        {
            MXB_ERROR("Element %lu in the `%s` array has an invalid value "
                      "\"%s\" for 'attribute'.",
                      index,
                      zArray_name,
                      json_string_value(pA));
        }
    }
    else
    {
        MXB_ERROR("Element %lu in the `%s` array does not contain "
                  "'attribute', 'op' and/or 'value', or one or all of them "
                  "is not a string.",
                  index,
                  zArray_name);
    }

    return pRule;
}

//static
bool CacheRules::get_attribute(const Attributes& valid_attributes,
                               const char* z,
                               CacheRule::Attribute* pAttribute)
{
    CacheRule::Attribute attribute;

    bool rv = CacheRule::from_string(z, &attribute);

    if (rv)
    {
        if (valid_attributes.count(attribute) != 0)
        {
            *pAttribute = attribute;
        }
        else
        {
            rv = false;
        }
    }

    return rv;
}

bool CacheRules::parse_store_element(json_t* pObject, size_t index)
{
    CacheRule* pRule = parse_element(pObject, KEY_STORE, index, s_store_attributes);

    if (pRule)
    {
        m_store_rules.emplace_back(static_cast<CacheRuleValue*>(pRule));
    }

    return pRule != nullptr;
}

bool CacheRules::parse_use_element(json_t* pObject, size_t index)
{
    CacheRule* pRule = parse_element(pObject, KEY_USE, index, s_use_attributes);

    if (pRule)
    {
        m_use_rules.emplace_back(static_cast<CacheRuleUser*>(pRule));
    }

    return pRule != nullptr;
}

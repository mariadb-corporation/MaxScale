/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "tokenizer.hh"

#include <cstring>
#include <unordered_map>

#define MAKE_VALUE(a) { #a, a}

namespace tok
{

// SQL keyword to enum value map, must stay in sync with the enum values defined in tokenizer.hh
const std::unordered_map<std::string, Type> Tokenizer::s_tokens =
{
    MAKE_VALUE(ADD),
    MAKE_VALUE(AFTER),
    MAKE_VALUE(ALTER),
    MAKE_VALUE(AUTO_INCREMENT),
    MAKE_VALUE(CASCADE),
    MAKE_VALUE(CHANGE),
    MAKE_VALUE(COLUMN_FORMAT),
    MAKE_VALUE(COLUMN),
    MAKE_VALUE(COMMENT),
    MAKE_VALUE(CONSTRAINT),
    MAKE_VALUE(CREATE),
    MAKE_VALUE(DEFAULT),
    MAKE_VALUE(DROP),
    MAKE_VALUE(DYNAMIC),
    MAKE_VALUE(EXISTS),
    MAKE_VALUE(FIRST),
    MAKE_VALUE(FIXED),
    MAKE_VALUE(FOREIGN),
    MAKE_VALUE(FULLTEXT),
    MAKE_VALUE(IF),
    MAKE_VALUE(IGNORE),
    MAKE_VALUE(INDEX),
    MAKE_VALUE(INVISIBLE),
    MAKE_VALUE(KEY),
    MAKE_VALUE(KEYS),
    MAKE_VALUE(LIKE),
    MAKE_VALUE(MODIFY),
    MAKE_VALUE(NOT),
    MAKE_VALUE(ONLINE),
    MAKE_VALUE(OR),
    MAKE_VALUE(PERIOD),
    MAKE_VALUE(PRIMARY),
    MAKE_VALUE(REF_SYSTEM_ID),
    MAKE_VALUE(REMOVE),
    MAKE_VALUE(RENAME),
    MAKE_VALUE(REPLACE),
    MAKE_VALUE(RESTRICT),
    MAKE_VALUE(SPATIAL),
    MAKE_VALUE(SYSTEM),
    MAKE_VALUE(TABLE),
    MAKE_VALUE(TO),
    MAKE_VALUE(UNIQUE),
    MAKE_VALUE(UNSIGNED),
    MAKE_VALUE(VERSIONING),
    MAKE_VALUE(WITH),
    MAKE_VALUE(WITHOUT),
    MAKE_VALUE(ZEROFILL),

    {"NULL",                   SQLNULL},
};

std::string Tokenizer::Token::to_string() const
{
    for (const auto& a : Tokenizer::s_tokens)
    {
        if (a.second == type())
        {
            return a.first;
        }
    }

    switch (type())
    {
    case DOT:
        return ".";

    case COMMA:
        return ",";

    case LP:
        return "(";

    case RP:
        return ")";

    case EQ:
        return "=";

    case ID:
        return "ID[" + value() + "]";

    default:
        return "UNKNOWN";
    }
}

std::string Tokenizer::Token::value() const
{
    return m_sanitizer(m_str, m_len);
}

const char* find_char(const char* s, char c)
{
    while (*s)
    {
        if (*s == '\\')
        {
            ++s;
        }
        else if (*s == c)
        {
            break;
        }

        ++s;
    }

    return s;
}

bool is_special(char c)
{
    switch (c)
    {
    case '.':
    case ',':
    case '(':
    case ')':
    case '`':
    case '\'':
    case '"':
    case '=':
        return true;

    default:
        return isspace(c);
    }
}

Tokenizer::Chain Tokenizer::tokenize(const char* sql, Sanitizer sanitizer)
{
    Tokenizer::Chain rval;
    std::string buf;

    while (char c = *sql)
    {
        switch (c)
        {
        case '.':
            rval.m_tokens.emplace_back(DOT, sql);
            ++sql;
            break;

        case '=':
            rval.m_tokens.emplace_back(EQ, sql);
            ++sql;
            break;

        case ',':
            rval.m_tokens.emplace_back(COMMA, sql);
            ++sql;
            break;

        case '(':
            rval.m_tokens.emplace_back(LP, sql);
            ++sql;
            break;

        case ')':
            rval.m_tokens.emplace_back(RP, sql);
            ++sql;
            break;

        case '`':
            ++sql;
            if (auto s = find_char(sql, '`'))
            {
                rval.m_tokens.emplace_back(ID, sql, s - sql, sanitizer);
                sql = s + 1;
            }
            else
            {
                return rval;    // Abort tokenization
            }

            break;

        case '\'':
        case '"':
            ++sql;
            if (auto s = find_char(sql, c))
            {
                rval.m_tokens.emplace_back(ID, sql, s - sql, sanitizer);
                sql = s + 1;
            }
            else
            {
                return rval;    // Abort tokenization
            }
            break;

        default:
            if (isspace(c))
            {
                ++sql;
            }
            else
            {
                const char* start = sql;
                buf.clear();

                while (*sql && !is_special(*sql))
                {
                    buf += toupper(*sql++);
                }

                Type type = ID;
                auto it = s_tokens.find(buf);

                if (it != s_tokens.end())
                {
                    type = it->second;
                }

                rval.m_tokens.emplace_back(type, start, sql - start, sanitizer);
            }

            break;
        }
    }

    return rval;
}

bool operator==(const Tokenizer::Token& lhs, const Tokenizer::Token& rhs)
{
    return lhs.type() == rhs.type();
}
}

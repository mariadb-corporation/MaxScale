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

#define MXS_MODULE_NAME "hintfilter"

#include <unordered_map>

#include <maxscale/filter.hh>
#include <maxscale/buffer.hh>

#include "mysqlhint.hh"

/**
 * Code for parsing SQL comments and processing them into MaxScale hints
 */

using InputIter = mxs::Buffer::iterator;

/* Parser tokens for the hint parser */
typedef enum
{
    TOK_MAXSCALE = 1,
    TOK_PREPARE,
    TOK_START,
    TOK_STOP,
    TOK_EQUAL,
    TOK_STRING,
    TOK_ROUTE,
    TOK_TO,
    TOK_MASTER,
    TOK_SLAVE,
    TOK_SERVER,
    TOK_LAST,
    TOK_LINEBRK,
    TOK_END
} TOKEN_VALUE;

/**
 * Advance an iterator until either an unescaped character `c` is found or `end` is reached
 *
 * @param it  Iterator to start from
 * @param end Past-the-end iterator
 * @param c   The character to look for
 *
 * @return The iterator pointing at the first occurrence of the character or `end` if one was not found
 */
InputIter skip_until(InputIter it, InputIter end, char c)
{
    while (it != end)
    {
        if (*it == '\\')
        {
            if (++it == end)
            {
                continue;
            }
        }
        else if (*it == c)
        {
            break;
        }

        ++it;
    }

    return it;
}

/**
 * Extract a MariaDB comment
 *
 * @param it  Iterator pointing to the start of the search string
 * @param end Past-the-end iterator
 *
 * @return A pair of iterators pointing to the range the comment spans. The comment tags themselves are not
 *         included in this range. If no comment is found, a pair of `end` iterators is returned.
 */
std::pair<InputIter, InputIter> get_comment(InputIter it, InputIter end)
{
    while (it != end)
    {
        switch (*it)
        {
        case '\\':
            if (++it == end)
            {
                continue;
            }
            break;

        case '"':
        case '\'':
        case '`':
            // Quoted literal string or identifier
            if ((it = skip_until(std::next(it), end, *it)) == end)
            {
                // Malformed quoted value
                continue;
            }
            break;

        case '#':
            // A comment that spans the rest of the line
            ++it;
            return {it, skip_until(it, end, '\n')};

        case '-':
            if (++it != end && *it == '-' && ++it != end && *it == ' ')
            {
                ++it;
                return {it, skip_until(it, end, '\n')};
            }
            continue;

        case '/':
            if (++it != end && *it == '*' && ++it != end)
            {
                auto start = it;

                while (it != end)
                {
                    auto comment_end = skip_until(it, end, '*');
                    it = comment_end;

                    if (it != end && ++it != end && *it == '/')
                    {
                        return {start, comment_end};
                    }
                }
            }
            continue;

        default:
            break;
        }

        ++it;
    }

    return {end, end};
}

/**
 * Extract all MariaDB comments from a query
 *
 * @param start Iterator position to start from
 * @param end   Past-the-end iterator
 *
 * @return A list of iterator pairs pointing to all comments in the query
 */
std::vector<std::pair<InputIter, InputIter>> get_all_comments(InputIter start, InputIter end)
{
    std::vector<std::pair<InputIter, InputIter>> rval;

    do
    {
        auto comment = get_comment(start, end);

        if (comment.first != comment.second)
        {
            rval.push_back(comment);
        }

        start = comment.second;
    }
    while (start != end);

    return rval;
}

// Simple container for two iterators and a token type
struct Token
{
    InputIter   begin;
    InputIter   end;
    TOKEN_VALUE type;
};

static const std::unordered_map<std::string, TOKEN_VALUE> tokens
{
    {"begin", TOK_START},
    {"end", TOK_STOP},
    {"last", TOK_LAST},
    {"master", TOK_MASTER},
    {"maxscale", TOK_MAXSCALE},
    {"prepare", TOK_PREPARE},
    {"route", TOK_ROUTE},
    {"server", TOK_SERVER},
    {"slave", TOK_SLAVE},
    {"start", TOK_START},
    {"stop", TOK_STOP},
    {"to", TOK_TO},
};

/**
 * Extract the next token
 *
 * @param it  Iterator to start from, modified to point to next non-whitespace character after the token
 * @param end Past-the-end iterator
 *
 * @return The next token
 */
Token next_token(InputIter* iter, InputIter end)
{
    InputIter& it = *iter;

    while (it != end && isspace(*it))
    {
        ++it;
    }

    TOKEN_VALUE type = TOK_END;
    auto start = it;

    if (it != end)
    {
        if (*it == '=')
        {
            ++it;
            type = TOK_EQUAL;
        }
        else
        {
            while (it != end && !isspace(*it) && *it != '=')
            {
                ++it;
            }

            auto t = tokens.find(std::string(start, it));

            if (t != tokens.end())
            {
                type = t->second;
            }
        }

        if (type == TOK_END && start != it)
        {
            // We read a string identifier
            type = TOK_STRING;
        }
    }

    return {start, it, type};
}

/**
 * Process the definition of a hint
 *
 * @param it  Start iterator
 * @param end End iterator
 *
 * @return The processed hint or NULL on invalid input
 */
HINT* process_definition(InputIter it, InputIter end)
{
    HINT* rval = nullptr;
    auto t = next_token(&it, end);

    if (t.type == TOK_ROUTE)
    {
        if (next_token(&it, end).type == TOK_TO)
        {
            t = next_token(&it, end);

            if (t.type == TOK_MASTER)
            {
                rval = hint_create_route(nullptr, HINT_ROUTE_TO_MASTER, nullptr);
            }
            else if (t.type == TOK_SLAVE)
            {
                rval = hint_create_route(nullptr, HINT_ROUTE_TO_SLAVE, nullptr);
            }
            else if (t.type == TOK_LAST)
            {
                rval = hint_create_route(nullptr, HINT_ROUTE_TO_LAST_USED, nullptr);
            }
            else if (t.type == TOK_SERVER)
            {
                t = next_token(&it, end);

                if (t.type == TOK_STRING)
                {
                    std::string value(t.begin, t.end);
                    rval = hint_create_route(nullptr, HINT_ROUTE_TO_NAMED_SERVER, value.c_str());
                }
            }
        }
    }
    else if (t.type == TOK_STRING)
    {
        std::string key(t.begin, t.end);
        auto eq = next_token(&it, end);
        auto val = next_token(&it, end);

        if (eq.type == TOK_EQUAL && val.type == TOK_STRING)
        {
            std::string value(val.begin, val.end);
            rval = hint_create_parameter(nullptr, key.c_str(), value.c_str());
        }
    }

    if (rval && next_token(&it, end).type != TOK_END)
    {
        // Unexpected input after hint definition, treat it as an error and remove the hint
        hint_free(rval);
        rval = nullptr;
    }

    return rval;
}

HINT* HINT_SESSION::process_comment(InputIter it, InputIter end)
{
    HINT* rval = nullptr;

    if (next_token(&it, end).type == TOK_MAXSCALE)
    {
        // Peek at the next token
        auto prev_it = it;
        auto t = next_token(&it, end);

        if (t.type == TOK_START)
        {
            if ((rval = process_definition(it, end)))
            {
                stack.push_back(hint_dup(rval));
            }
        }
        else if (t.type == TOK_STOP)
        {
            if (!stack.empty())
            {
                hint_free(stack.back());
                stack.pop_back();
            }
        }
        else if (t.type == TOK_STRING)
        {
            std::string key(t.begin, t.end);
            t = next_token(&it, end);

            if (t.type == TOK_EQUAL)
            {
                t = next_token(&it, end);

                if (t.type == TOK_STRING)
                {
                    // A key=value hint
                    std::string value(t.begin, t.end);
                    rval = hint_create_parameter(nullptr, key.c_str(), value.c_str());
                }
            }
            else if (t.type == TOK_PREPARE)
            {
                HINT* hint = process_definition(it, end);

                if (hint)
                {
                    // Preparation of a named hint
                    named_hints[key] = hint_dup(hint);
                }
            }
            else if (t.type == TOK_START)
            {
                if ((rval = process_definition(it, end)))
                {
                    if (named_hints.count(key) == 0)
                    {
                        // New hint defined, push it on to the stack
                        named_hints[key] = hint_dup(rval);
                        stack.push_back(hint_dup(rval));
                    }
                }
                else if (next_token(&it, end).type == TOK_END)
                {
                    auto it = named_hints.find(key);

                    if (it != named_hints.end())
                    {
                        // We're starting an already define named hint
                        stack.push_back(hint_dup(it->second));
                        rval = hint_dup(it->second);
                    }
                }
            }
        }
        else
        {
            // Only hint definition in the comment, use the stored iterator to process it again
            rval = process_definition(prev_it, end);
        }
    }

    return rval;
}

void HINT_SESSION::process_hints(GWBUF* buffer)
{
    mxs::Buffer buf(buffer);

    for (auto comment : get_all_comments(std::next(buf.begin(), 5), buf.end()))
    {
        HINT* hint = process_comment(comment.first, comment.second);

        if (hint)
        {
            buffer->hint = hint_splice(buffer->hint, hint);
        }
    }

    if (!buffer->hint && !stack.empty())
    {
        buffer->hint = hint_dup(stack.back());
    }

    buf.release();
}

#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/hint.h>
#include <maxscale/filter.hh>

namespace std
{
template<>
struct default_delete<HINT>
{
    void operator()(HINT* pHint)
    {
        hint_free(pHint);
    }
};
}

class HintSession;

class HintInstance : public mxs::Filter
{
public:
    static HintInstance*        create(const char* zName, mxs::ConfigParameters* ppParams);
    mxs::FilterSession*         newSession(MXS_SESSION* pSession, SERVICE* pService);
    json_t*                     diagnostics() const;
    uint64_t                    getCapabilities() const;
    mxs::config::Configuration* getConfiguration();
};

enum TOKEN_VALUE
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
};

// Class that parses text into MaxScale hints
class HintParser
{
public:
    using InputIter = mxs::Buffer::iterator;

    /**
     * Parse text into a hint
     *
     * @param begin InputIterator pointing to the start of the text
     * @param end   InputIterator pointing to the end of the text
     *
     * @return The parsed hint if a valid one was found
     */
    HINT* parse(InputIter begin, InputIter end);

private:

    InputIter m_it;
    InputIter m_end;
    InputIter m_tok_begin;
    InputIter m_tok_end;

    std::vector<std::unique_ptr<HINT>>                     m_stack;
    std::unordered_map<std::string, std::unique_ptr<HINT>> m_named_hints;

    TOKEN_VALUE next_token();
    HINT*       process_definition();
    HINT*       parse_one(InputIter begin, InputIter end);
};

class HintSession : public mxs::FilterSession
{
public:
    HintSession(const HintSession&) = delete;
    HintSession& operator=(const HintSession&) = delete;

    HintSession(MXS_SESSION* session, SERVICE* service);
    int routeQuery(GWBUF* queue);

private:
    HintParser m_parser;

    void process_hints(GWBUF* buffer);
};

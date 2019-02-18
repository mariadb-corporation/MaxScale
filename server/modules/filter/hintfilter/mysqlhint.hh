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

#include <maxscale/ccdefs.hh>
#include <maxscale/hint.h>
#include <maxscale/filter.hh>

class HINT_SESSION;

class HINT_INSTANCE : public mxs::Filter<HINT_INSTANCE, HINT_SESSION>
{
public:
    static HINT_INSTANCE* create(const char* zName, MXS_CONFIG_PARAMETER* ppParams);
    HINT_SESSION*         newSession(MXS_SESSION* pSession);
    void                  diagnostics(DCB* pDcb) const;
    json_t*               diagnostics_json() const;
    uint64_t              getCapabilities();
};

class HINT_SESSION : public mxs::FilterSession
{
public:
    HINT_SESSION(MXS_SESSION* session);
    ~HINT_SESSION();
    int routeQuery(GWBUF* queue);

private:
    std::vector<HINT*>                     stack;
    std::unordered_map<std::string, HINT*> named_hints;

    using InputIter = mxs::Buffer::iterator;
    HINT* process_comment(InputIter it, InputIter end);
    void  process_hints(GWBUF* buffer);

    // Unit testing functions
    friend void count_hints(const std::string& input, int num_expected);
    friend void test_parse(const std::string& input, int expected_type);
};

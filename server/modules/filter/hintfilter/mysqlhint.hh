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

/**
 * The hint instance structure
 */
struct HINT_INSTANCE: public MXS_FILTER
{
    int sessions = 0;
};

/**
 * A hint parser session structure
 */
struct HINT_SESSION: public MXS_FILTER_SESSION
{
    MXS_DOWNSTREAM down;
    std::vector<HINT*> stack;
    std::unordered_map<std::string, HINT*> named_hints;
};

void         process_hints(HINT_SESSION* session, GWBUF* buffer);

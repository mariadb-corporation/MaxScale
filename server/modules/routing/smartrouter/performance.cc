/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "performance.hh"

std::string show_some(const std::string& str, int nchars)
{
    int sz = str.length();
    if (sz > nchars)
    {
        return str.substr(0, nchars) + "...";
    }
    else
    {
        return str;
    }
}

// These are TODOs for the GA version. The Beta version will not have persistence.
// 1. Read the file once at startup. There might also be a need to do cleanup
//    of the file if the configuration has changed.
// 2. Updates to the data should "quickly" become globally available,
//    rather than be written after every change.
// 3. Writing the file back should go through something like of lockless queue,
//    that triggers a write, possibly when there is less load on the system.
// 4. Every now and then some form of re-learn, maybe just dropping entries after
//    some expiration time.
// 5. If a host goes away (even for maintenance) entries associated with it should
//    probably be dropped immediately.
// 6. Save all data at shutdown.
// Start using xxhash

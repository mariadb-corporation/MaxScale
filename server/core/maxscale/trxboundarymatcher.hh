#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/pcre2.h>
#include <maxscale/query_classifier.h>

namespace maxscale
{

/**
 * @class TrxBoundaryMatcher
 *
 * @ TrxBoundaryMatcher is a class capable of, using regexes, to recognize
 * and return the correct type mask of statements affecting the transaction
 * state and* autocommit mode.
 */
class TrxBoundaryMatcher
{
public:
    /**
     * To be called once at process startup. After a successful call,
     * @type_mask_of() can be used in the calling thread.
     *
     * @return True, if the initialization succeeded, false otherwise.
     */
    static bool process_init();

    /**
     * To be called once at process shut down.
     */
    static void process_end();

    /**
     * To be called once in each thread, other that the one where @process_init()
     * was called. After a successful call, @type_mask_of() can be used in the
     * calling thread.
     *
     * @return True, if the initialization succeeded, false otherwise.
     */
    static bool thread_init();

    /**
     * To be called once at thread shut down.
     */
    static void thread_end();

    /**
     * Return the type mask of a statement, provided the statement affects
     * transaction state or autocommit mode.
     *
     * @param pSql  Sql statement.
     * @param len   Length of @c pSql.
     *
     * @return The corresponding type mask or 0, if the statement does not
     *         affect transaction state or autocommit mode.
     */
    static uint32_t type_mask_of(const char* pSql, size_t len);

    /**
     * Return the type mask of a statement, provided the statement affects
     * transaction state or autocommit mode.
     *
     * @param pBuf A COM_QUERY
     *
     * @return The corresponding type mask or 0, if the statement does not
     *         affect transaction state or autocommit mode.
     */
    static uint32_t type_mask_of(GWBUF* pBuf);

private:
    TrxBoundaryMatcher(const TrxBoundaryMatcher&);
    TrxBoundaryMatcher& operator = (const TrxBoundaryMatcher&);
};

}

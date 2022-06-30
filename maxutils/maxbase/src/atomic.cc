/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/atomic.hh>

int32_t atomic_load_int32(const int32_t* variable)
{
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
}

void atomic_store_int32(int32_t* variable, int32_t value)
{
    __atomic_store_n(variable, value, __ATOMIC_SEQ_CST);
}


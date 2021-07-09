/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

namespace
{
struct ThisUnit
{
    bool is_test = false;
};

static ThisUnit this_unit;
}

namespace maxscale
{
namespace test
{
bool is_test()
{
    return this_unit.is_test;
}
void start_test()
{
    this_unit.is_test = true;
}
}
}

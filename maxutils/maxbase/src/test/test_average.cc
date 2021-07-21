/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/log.hh>
#include <maxbase/average.hh>

using namespace mxb;

int main()
{
    Log log;
    AverageN ave(6);

    ave.add_value(10);
    ave.add_value(20);
    ave.add_value(30);
    ave.add_value(40);
    ave.add_value(50);
    ave.add_value(60);

    mxb_assert(ave.value() == 35); // (10 + 20 + 30 + 40 + 50 + 60) / 6 = 35

    ave.add_value(70); // Should cause 10 to drop off.

    mxb_assert(ave.value() == 45); // (20 + 30 + 40 + 50 + 60 + 70) / 6 = 45

    ave.resize(5); // Should cause 20 to drop off.

    mxb_assert(ave.value() == 50); // (30 + 40 + 50 + 60 + 70) / 5 = 50

    ave.resize(7); // Nothing should happen

    mxb_assert(ave.value() == 50); // (30 + 40 + 50 + 60 + 70) / 5 = 50

    ave.add_value(80);

    mxb_assert(ave.value() == 55); // (30 + 40 + 50 + 60 + 70 + 80) / 6 = 55

    ave.resize(2);

    mxb_assert(ave.value() == 75); // (70 + 80) / 2 = 75

    ave.resize(1);

    ave.add_value(42);

    mxb_assert(ave.value() == 42); // 42 / 1 = 42

    AverageN ave2(1);

    ave2.resize(10);

    return 0;
}

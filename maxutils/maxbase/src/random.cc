/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/random.hh>
#include <random>

namespace maxbase
{

static uint64_t splitmix(uint64_t& state)
{
    uint64_t z = (state += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

XorShiftRandom::XorShiftRandom(uint64_t seed)
{
    if (!seed)
    {
        std::random_device rdev;
        while (!(seed = rdev()))
        {
        }
    }
    for (auto& s : m_state)
    {
        s = splitmix(seed);
    }
}
}

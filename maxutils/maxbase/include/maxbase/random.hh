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
#pragma once

#include <maxbase/ccdefs.hh>

#include <array>

namespace maxbase
{

/**
 * XorShiftRandom is an extremely fast all purpose random number generator.
 *
 * Uses xoshiro256** http://xoshiro.di.unimi.it written in 2018 by David Blackman
 * and Sebastiano Vigna. Comment from the source code:
 * This is xoshiro256** 1.0, our all-purpose, rock-solid generator. It has
 * excellent (sub-ns) speed, a state (256 bits) that is large enough for
 * any parallel application, and it passes all tests we are aware of.
 */
class XorShiftRandom
{
public:
    // Non-deterministic if seed == 0
    explicit XorShiftRandom(uint64_t seed = 0);
    uint64_t rand();
    uint32_t rand32();
    bool     rand_bool();
    int64_t  b_to_e_co(int64_t b, int64_t e);
    double   zero_to_one_co();
private:
    uint64_t rotl(const uint64_t x, int k);
    std::array<uint64_t, 4> m_state;
};

// *********************************
// inlined functions below this line

inline uint64_t XorShiftRandom::rotl(const uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

inline uint64_t XorShiftRandom::rand()
{
    // xoshiro256**
    const uint64_t result_starstar = rotl(m_state[1] * 5, 7) * 9;

    const uint64_t t = m_state[1] << 17;

    m_state[2] ^= m_state[0];
    m_state[3] ^= m_state[1];
    m_state[1] ^= m_state[2];
    m_state[0] ^= m_state[3];

    m_state[2] ^= t;

    m_state[3] = rotl(m_state[3], 45);

    return result_starstar;
}

inline uint32_t XorShiftRandom::rand32()
{
    return rand() >> 32;    // shifting, although low bits are good
}

inline bool XorShiftRandom::rand_bool()
{
    return rand() % 2;
}

inline double XorShiftRandom::zero_to_one_co()
{
    uint64_t x = rand();

    // Turn uint64_t to a [0,1[ double (yes, it's standard and portable)
    const union
    {
        uint64_t i;
        double   d;
    } u = {.i = UINT64_C(0x3FF) << 52 | x >> 12};

    return u.d - 1.0;
}

inline int64_t XorShiftRandom::b_to_e_co(int64_t b, int64_t e)
{
    // With 64 bits mod bias does not happen in practise (a very, very large e-b would be needed).
    // alternative: return b + int64_t(zero_to_one_exclusive()*(e-b));
    return b + rand() % (e - b);
}
}

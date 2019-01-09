/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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
#pragma once

#include <maxbase/ccdefs.hh>

#include <random>
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
    int64_t  b_to_e_exclusive(int64_t b, int64_t e);
    double   zero_to_one_exclusive();
private:
    uint64_t rotl(const uint64_t x, int k);
    std::array<uint64_t, 4> m_state;
};

/**
 * StdTwisterRandom is a class for random number generation using the C++ standard library.
 * Uses the Mersenne Twister algorithms (mt19937 and mt19937_64).
 */
class StdTwisterRandom
{
public:
    // Non-deterministic if seed == 0
    explicit StdTwisterRandom(uint64_t seed = 0);

    uint64_t rand();
    uint32_t rand32();
    bool     rand_bool();
    int64_t  b_to_e_exclusive(int64_t b, int64_t e);
    double   zero_to_one_exclusive();

    // Borrow the mt19937_64 engine for other distributions.
    std::mt19937_64& rnd_engine();
private:
    std::mt19937    m_twister_engine_32;
    std::mt19937_64 m_twister_engine_64;
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
    return std::signbit(int64_t(rand()));
}

inline double XorShiftRandom::zero_to_one_exclusive()
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

inline int64_t XorShiftRandom::b_to_e_exclusive(int64_t b, int64_t e)
{
    // With 64 bits mod bias does not happen in practise (a very, very large e-b would be needed).
    // alternative: return b + int64_t(zero_to_one_exclusive()*(e-b));
    return b + rand() % (e - b);
}

inline uint64_t StdTwisterRandom::rand()
{
    return m_twister_engine_64();
}

inline uint32_t StdTwisterRandom::rand32()
{
    return m_twister_engine_32();
}

inline bool StdTwisterRandom::rand_bool()
{
    return std::signbit(int32_t(rand32()));
}

inline double StdTwisterRandom::zero_to_one_exclusive()
{
    std::uniform_real_distribution<double> zero_to_one {0, 1};
    return zero_to_one(m_twister_engine_64);
}

inline int64_t StdTwisterRandom::b_to_e_exclusive(int64_t b, int64_t e)
{
    std::uniform_int_distribution<int64_t> dist {b, e - 1};
    return dist(m_twister_engine_64);
}
}

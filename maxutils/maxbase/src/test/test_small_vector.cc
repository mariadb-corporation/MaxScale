/*
 * Copyright (c) 2023 MariaDB plc
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

#ifndef SS_DEBUG
#define SS_DEBUG
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <maxbase/ccdefs.hh>
#include <maxbase/small_vector.hh>

static int64_t ints = 0;

int identity(int i)
{
    return i;
}

void* int_to_ptr(int i)
{
    std::intptr_t p = i;
    return (void*)p;
}

std::string int_to_string(int i)
{
    return std::to_string(i);
}

template<size_t Size, class Type, auto Generator>
constexpr void run_test(size_t num_values)
{
    mxb::small_vector<Type, Size> small_vec;
    std::vector<Type> vec;

    const auto do_check = [&](){
        for (size_t i = 0; i < vec.size(); i++)
        {
            mxb_assert(vec[i] == small_vec[i]);
        }
        mxb_assert(vec.size() == small_vec.size());
        mxb_assert(vec.empty() == small_vec.empty());

        if (!vec.empty())
        {
            mxb_assert(vec.front() == small_vec.front());
            mxb_assert(vec.back() == small_vec.back());
        }
    };

    for (size_t i = 0; i < num_values; i++)
    {
        Type t = Generator(ints++);
        small_vec.push_back(t);
        vec.push_back(t);
        do_check();
    }

    if (Size / 2 < vec.size())
    {
        vec.erase(vec.begin() + Size / 2);
        small_vec.erase(small_vec.begin() + Size / 2);
        do_check();
    }

    while (!vec.empty())
    {
        vec.erase(vec.begin());
        small_vec.erase(small_vec.begin());
        do_check();
    }
}

template<size_t ... Size>
constexpr void run_all_tests(size_t num_values, std::index_sequence<Size...>)
{
    (run_test<Size, int, identity>(num_values), ...);
    (run_test<Size, char, identity>(num_values), ...);
    (run_test<Size, size_t, identity>(num_values), ...);
    (run_test<Size, float, identity>(num_values), ...);
    (run_test<Size, void*, int_to_ptr>(num_values), ...);
    (run_test<Size, std::string, int_to_string>(num_values), ...);
}

int main(int argc, char* argv[])
{
    mxb::Log logger(MXB_LOG_TARGET_STDOUT);

    for (int i = 1; i <= 31; i += 3)
    {
        run_all_tests(i, std::index_sequence<1, 2, 3, 4, 5, 11, 29> {});
    }

    return 0;
}

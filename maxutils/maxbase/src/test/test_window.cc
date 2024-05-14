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

#include <vector>
#include <deque>
#include <iostream>

#include <maxbase/ccdefs.hh>
#include <maxbase/window.hh>
#include <maxbase/string.hh>

const char* dump(const std::vector<int64_t>& v, const std::deque<int64_t>& d)
{
    static std::string errmsg;
    errmsg = mxb::cat("Window: {", mxb::join(v), "} Queue: {", mxb::join(d), "}");
    return errmsg.c_str();
}

void run_test(size_t size, size_t num_values)
{
    mxb::Window<int64_t> r(0);
    std::deque<int64_t> d;

    auto fill_and_check = [&](double factor){
        size_t sz = size * factor;

        if (num_values == 0)
        {
            r.clear();
            d.clear();
        }

        auto tmp = mxb::Window<int64_t>(sz, std::move(r));
        r = std::move(tmp);

        for (size_t i = 0; i < num_values; i++)
        {
            int64_t t = num_values - i;
            r.push(t);
            d.push_back(t);

            while (d.size() > sz)
            {
                d.pop_front();
            }
        }

        std::vector<int64_t> v(r.begin(), r.end());
        mxb_assert(v.size() == (size_t)std::distance(r.begin(), r.end()));
        mxb_assert_message(v.empty() || v.back() == 1, "%s", dump(v, d));
        mxb_assert_message(v.size() == d.size(), "%s", dump(v, d));
        mxb_assert_message(std::equal(v.begin(), v.end(), d.begin(), d.end()), "%s", dump(v, d));
    };

    // Basic check
    fill_and_check(1);
    // Make sure the container grows if requested
    fill_and_check(2);
    // The container should also shrink
    fill_and_check(0.5);
}

int main(int argc, char* argv[])
{
    mxb::Log logger(MXB_LOG_TARGET_STDOUT);

    for (int i = 0; i <= 64; i++)
    {
        for (int j = 0; j <= 64; j++)
        {
            run_test(i, j);
        }
    }

    return 0;
}

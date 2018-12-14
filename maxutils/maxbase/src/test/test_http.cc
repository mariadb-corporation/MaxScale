/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxbase/http.hh>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <maxbase/log.hh>

using namespace std;

namespace
{

int test_http()
{
    cout << __func__ << endl;

    int rv = EXIT_FAILURE;

    auto res = mxb::http::get("http://www.example.com/");
    cout << "http://www.example.com/ responded with: " << res.code << endl;
    if (res.code == 200)
    {
        if (res.headers.count("Date"))
        {
            cout << "The date is: " << res.headers["Date"] << endl;
            rv = EXIT_SUCCESS;
        }
    }
    else
    {
        cout << "error: Exit code not 200 but: " << res.code << endl;
    }

    return rv;
}

int test_multi_http()
{
    cout << __func__ << endl;

    int rv = EXIT_SUCCESS;

    vector<string> urls = { "http://www.example.com/", "http://www.example.com/", "http://non-existent.xyz" };
    vector<bool> expected_successes = { true, true, false };
    vector<mxb::http::Result> results = mxb::http::get(urls);

    for (size_t i = 0; i < urls.size(); ++i)
    {
        const auto& url = urls[i];
        auto& res = results[i];
        bool expected_success = expected_successes[i];

        cout << url << " responded with: " << res.code << endl;

        if (expected_success)
        {
            if (res.code == 200)
            {
                if (res.headers.count("Date"))
                {
                    cout << "The date is: " << res.headers["Date"] << endl;
                }
                else
                {
                    rv = EXIT_FAILURE;
                }
            }
            else
            {
                rv = EXIT_FAILURE;
            }
        }
        else
        {
            if (res.code != 0)
            {
                rv = EXIT_FAILURE;
            }
        }
    }

    return rv;
}

}

uint64_t time_since_epoch_ms()
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}


int main()
{
    int rv = EXIT_SUCCESS;
    mxb::Log log;

    auto start = time_since_epoch_ms();
    rv += test_http();
    auto stop = time_since_epoch_ms();
    cout << "Single: " << stop - start << endl;

    start = time_since_epoch_ms();
    rv += test_multi_http();
    stop = time_since_epoch_ms();
    cout << "Multi: " << stop - start << endl;

    return rv;
}

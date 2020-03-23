/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/http.hh>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <maxbase/log.hh>
#include <maxbase/stopwatch.hh>

using namespace std;

namespace
{

int test_http_get()
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

    return rv == EXIT_FAILURE ? 1 : 0;
}

int check_results(const vector<string>& urls,
                  const vector<bool>& expected_successes,
                  const vector<mxb::http::Result> results)
{
    int rv = EXIT_SUCCESS;

    for (size_t i = 0; i < urls.size(); ++i)
    {
        const auto& url = urls[i];
        auto& res = results[i];
        bool expected_success = expected_successes[i];

        cout << url << " responded with: " << res.code
             << (res.code < 0 ? (", " + res.body) : "") << endl;

        if (expected_success)
        {
            if (res.code == 200)
            {
                if (res.headers.count("Date"))
                {
                    cout << "The date is: " << res.headers.at("Date") << endl;
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
            switch (res.code)
            {
            case mxb::http::Result::ERROR:
            case mxb::http::Result::COULDNT_RESOLVE_HOST:
            case mxb::http::Result::OPERATION_TIMEDOUT:
                break;

            default:
                rv = EXIT_FAILURE;
            }
        }
    }

    return rv == EXIT_FAILURE ? 1 : 0;
}

int test_multi_http_get()
{
    cout << __func__ << endl;

    vector<string> urls = {"http://www.example.com/", "http://www.example.com/", "http://non-existent.xyz"};
    vector<bool> expected_successes = {true, true, false};
    vector<mxb::http::Result> results = mxb::http::get(urls);

    int rv = check_results(urls, expected_successes, results);

    return rv == EXIT_FAILURE ? 1 : 0;
}

int test_async_http_get()
{
    cout << __func__ << endl;

    int rv = EXIT_FAILURE;

    vector<string> urls = {"http://www.example.com/", "http://www.example.com/", "http://non-existent.xyz"};
    vector<bool> expected_successes = {true, true, false};
    mxb::http::Async http = mxb::http::get_async(urls);

    while (http.perform(0) == mxb::http::Async::PENDING)
    {
        long ms = http.wait_no_more_than();

        if (ms > 100)
        {
            ms = 100;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    if (http.status() == mxb::http::Async::READY)
    {
        const vector<mxb::http::Result>& results = http.results();

        rv = check_results(urls, expected_successes, results);
    }
    else
    {
        cout << "http::Async: " << to_string(http.status()) << endl;
        rv = EXIT_FAILURE;
    }

    return rv == EXIT_FAILURE ? 1 : 0;
}

int test_http_put(const vector<char>& body = vector<char>())
{
    cout << __func__ << endl;

    int rv = EXIT_FAILURE;

    auto res = mxb::http::put("http://postman-echo.com/put", body);
    cout << "http://postman-echo.com/put responded with: " << res.code << endl;
    if (res.code == 200)
    {
        cout << "MESSAGE:" << res.body << endl;
        rv = EXIT_SUCCESS;
    }
    else
    {
        cout << "error: Exit code not 200 but: " << res.code << endl;
    }

    return rv == EXIT_FAILURE ? 1 : 0;
}

int test_async_http_put(const vector<char>& body = vector<char>())
{
    cout << __func__ << endl;

    int rv = EXIT_FAILURE;

    vector<string> urls = {"http://postman-echo.com/put",
                           "http://postman-echo.com/put",
                           "http://postman-echo.com/put"};
    vector<bool> expected_successes = {true, true, true};
    mxb::http::Async http = mxb::http::put_async(urls, body);

    while (http.perform(0) == mxb::http::Async::PENDING)
    {
        long ms = http.wait_no_more_than();

        if (ms > 100)
        {
            ms = 100;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    if (http.status() == mxb::http::Async::READY)
    {
        const vector<mxb::http::Result>& results = http.results();

        rv = check_results(urls, expected_successes, results);
    }
    else
    {
        cout << "http::Async: " << to_string(http.status()) << endl;
        rv = EXIT_FAILURE;
    }

    return rv == EXIT_FAILURE ? 1 : 0;
}

vector<char> create_body(const string& s)
{
    return vector<char>(s.begin(), s.end());
}

}

int main()
{
    int rv = 0;
    mxb::Log log;

    mxb::http::Init init;
    mxb::StopWatch sw;
    mxb::Duration d;

    sw.restart();
    rv += test_http_get();
    d = sw.split();
    cout << "Single GET: " << d << endl;

    sw.restart();
    rv += test_multi_http_get();
    d = sw.split();
    cout << "Multi GET: " << d << endl;

    sw.restart();
    rv += test_async_http_get();
    d = sw.split();
    cout << "Async GET: " << d << endl;

    sw.restart();
    rv += test_http_put();
    d = sw.split();
    cout << "Single PUT (no body): " << d << endl;

    sw.restart();
    rv += test_http_put(create_body("{ \"hello\": \"world\" }"));
    d = sw.split();
    cout << "Single PUT (with body): " << d << endl;

    sw.restart();
    rv += test_async_http_put();
    d = sw.split();
    cout << "Async PUT: " << d << endl;

    sw.restart();
    rv += test_async_http_put(create_body("{ \"hello\": \"world\" }"));
    d = sw.split();
    cout << "Async PUT (with body): " << d << endl;

    return rv;
}

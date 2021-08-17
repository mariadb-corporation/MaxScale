/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
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
#include <maxbase/assert.h>
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

int test_http_put(const string& body = string())
{
    cout << __func__ << endl;

    int rv = EXIT_FAILURE;

    json_t* pFrom = nullptr;

    if (body.size() != 0)
    {
        json_error_t error;
        pFrom = json_loadb(body.data(), body.size(), JSON_DISABLE_EOF_CHECK, &error);
        mxb_assert(pFrom);
    }

    mxb::http::Config config;
    config.headers["Content-Type"] = "application/json";
    config.headers["Accept"] = "*/*";
    auto res = mxb::http::put("http://postman-echo.com/put", body, config);
    cout << "http://postman-echo.com/put responded with: " << res.code << endl;
    if (res.code == 200)
    {
        cout << "BODY:" << res.body << endl;

        if (pFrom)
        {
            json_error_t error;
            json_t* pBody = json_loads(res.body.c_str(), JSON_DISABLE_EOF_CHECK, &error);
            mxb_assert(pBody);
            json_t* pTo = json_object_get(pBody, "data");
            mxb_assert(pTo);

            if (json_equal(pFrom, pTo))
            {
                rv = EXIT_SUCCESS;
            }
            else
            {
                cout << "Sent and returned JSON body not equal; sent = '"
                     << body.data() << "', received = '"
                     << res.body
                     << "'."
                     << endl;
            }

            json_decref(pBody);
        }
        else
        {
            rv = EXIT_SUCCESS;
        }
    }
    else
    {
        cout << "error: Exit code not 200 but: " << res.code << endl;
    }

    if (pFrom)
    {
        json_decref(pFrom);
    }

    return rv == EXIT_FAILURE ? 1 : 0;
}

int test_async_http_put(const string& body = string())
{
    cout << __func__ << endl;

    int rv = EXIT_FAILURE;

    vector<string> urls = {"http://postman-echo.com/put",
                           "http://postman-echo.com/put",
                           "http://postman-echo.com/put"};
    vector<bool> expected_successes = {true, true, true};
    mxb::http::Config config;
    config.headers["Content-Type"] = "application/json";
    config.headers["Accept"] = "*/*";
    mxb::http::Async http = mxb::http::put_async(urls, body, config);

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
    rv += test_http_put("{ \"hello\": \"world\" }");
    d = sw.split();
    cout << "Single PUT (with body): " << d << endl;

    sw.restart();
    rv += test_async_http_put();
    d = sw.split();
    cout << "Async PUT: " << d << endl;

    sw.restart();
    rv += test_async_http_put("{ \"hello\": \"world\" }");
    d = sw.split();
    cout << "Async PUT (with body): " << d << endl;

    return rv;
}

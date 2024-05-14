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

#include <maxtest/testconnections.hh>
#include <maxtest/maxrest.hh>
#include <maxbase/stopwatch.hh>

void test_main(TestConnections& test)
{
    std::atomic<bool> running {true};

    std::thread query_thr([&](){
        while (running)
        {
            auto c = test.maxscale->rwsplit();
            c.connect();
            c.query("SELECT 1");
        }
    });

    auto start = mxb::Clock::now();

    try
    {
        MaxRest api(&test, test.maxscale);
        api.fail_on_error(false);

        while (mxb::Clock::now() - start < 30s)
        {
            auto js = api.curl_get("sessions");

            for (auto s : js.at("data").get_array_elems())
            {
                auto attrs = s.at("attributes/client/connection_attributes");

                if (attrs.type() == mxb::Json::Type::JSON_NULL)
                {
                    test.tprintf("Found partially initialized session:\n%s",
                                 s.to_string(mxb::Json::Format::PRETTY).c_str());
                    start = mxb::Clock::time_point{};
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        test.add_failure("%s", e.what());
    }

    running = false;
    query_thr.join();
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}

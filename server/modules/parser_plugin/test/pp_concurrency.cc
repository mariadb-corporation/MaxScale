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

#include <maxscale/log.hh>
#include <maxscale/paths.hh>
#include <maxscale/parser.hh>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <unistd.h>

using namespace mxs;
using namespace std;

namespace
{

time_t           seconds = 10;
std::atomic<int> nStmts = 0;

vector<string_view> stmts =
{
    "BROKEN",
    "SELECT 1",
    "CREATE TABLE t (f INT)",
    "INSERT INTO t VALUES (1)",
};

void thread_main(Parser* pParser)
{
    static atomic<int> gid = 0;

    int tid = ++gid;

    pParser->plugin().thread_init();

    auto start = time(nullptr);

    int i = 0;
    while (time(nullptr) - start < seconds)
    {
        GWBUF stmt = pParser->helper().create_packet(stmts[i]);
        pParser->parse(stmt, Parser::COLLECT_ALL);
        ++nStmts;

        i = (i + 1) % stmts.size();
    }

    pParser->plugin().thread_end();
}

void print_usage_and_exit(const char* zProgram)
{
    cout << "usage: " << zProgram << " <plugin>" << endl;
    exit(1);
}

}


int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        print_usage_and_exit(argv[0]);
    }

    const char* zPlugin = argv[argc - 1];
    string libdir ("../");
    libdir += zPlugin;

    mxb::Log log;

    mxs::set_libdir(libdir.c_str());

    ParserPlugin* pPlugin = ParserPlugin::load(zPlugin);

    if (pPlugin)
    {
        auto sParser = pPlugin->create_parser(&pPlugin->default_helper());

        vector<thread> threads;
        for (size_t i = 0; i < 100; ++i)
        {
            threads.emplace_back(thread_main, sParser.get());
        }

        time_t s = seconds;

        do
        {
            cout << s-- << " " << flush;
            sleep(1);
        }
        while (s);

        cout << endl;

        for (size_t i = 0; i < threads.size(); ++i)
        {
            threads[i].join();
        }

        cout << "Stmts: " << nStmts << endl;
    }
    else
    {
        cerr << "error: Could not load." << endl;
    }

    return 0;
}

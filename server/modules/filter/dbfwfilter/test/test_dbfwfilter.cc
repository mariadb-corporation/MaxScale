/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <cstdlib>
#include <memory>
#include <iostream>
#include <maxscale/log_manager.h>
#include "maxscale/filtermodule.hh"
#include "maxscale/queryclassifiermodule.hh"
#include "maxscale/mock/backend.hh"
#include "maxscale/mock/routersession.hh"
#include "maxscale/mock/session.hh"
#include "maxscale/mock/upstream.hh"
#include "tempfile.hh"

using namespace std;
using maxscale::FilterModule;
using maxscale::QueryClassifierModule;
namespace mock = maxscale::mock;

namespace
{

enum fw_action_t
{
    FW_ACTION_ALLOW,
    FW_ACTION_BLOCK
};

struct FW_TEST_CASE
{
    const char* zStatement; /* The statement to test. */
    const char* zUser;      /* The user to test as. */
    const char* zHost;      /* The host of the user. */
    fw_action_t result;     /* The expected outcome. */
};

const char DEFAULT_HOST[] = "127.0.0.1";

const size_t N_MAX_CASES = 10;

struct FW_TEST
{
    const char*  zRules;             /* The firewall rules. */
    fw_action_t  action;             /* The firewall action. */
    FW_TEST_CASE cases[N_MAX_CASES]; /* The test cases to execute using the above settings. */
} FIREWALL_TESTS[] =
{
    {
        "rule no_select_a match columns a\n"
        "users bob@% match any rules no_select_a\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT a FROM t",
                "bob",
                DEFAULT_HOST,
                FW_ACTION_BLOCK
            }
        }
    },
    {
        "rule only_length_on_a match not_function length columns a\n"
        "users bob@% match any rules only_length_on_a\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT a FROM t",
                "bob",
                DEFAULT_HOST,
                FW_ACTION_ALLOW
            }
        }
    }
};

const size_t N_FIREWALL_TESTS = sizeof(FIREWALL_TESTS) / sizeof(FIREWALL_TESTS[0]);

}

namespace
{

void log_success(const FW_TEST_CASE& c)
{
    cout << "SUCCESS  : Statement ";

    if (c.result == FW_ACTION_ALLOW)
    {
        cout << "was expected to pass, and did pass." << endl;
    }
    else
    {
        cout << "was expected to be blocked, and was blocked." << endl;
    }
}

void log_error(const FW_TEST_CASE& c)
{
    cout << "ERROR    : Statement ";

    if (c.result == FW_ACTION_ALLOW)
    {
        cout << "was expected to pass, but did not pass." << endl;
    }
    else
    {
        cout << "was expected to be blocked, but was not blocked." << endl;
    }
}

int test(FilterModule::Session& filter_session,
         mock::RouterSession& router_session,
         const FW_TEST_CASE& c)
{
    int rv = 0;

    cout << "STATEMENT: " << c.zStatement << endl;
    cout << "CLIENT   : " << c.zUser << "@" << c.zHost << endl;

    GWBUF* pStatement = mock::create_com_query(c.zStatement);

    filter_session.routeQuery(pStatement);

    if (c.result == FW_ACTION_ALLOW)
    {
        if (!router_session.idle()) // Statement reached backend
        {
            router_session.discard_one_response();
            log_success(c);
        }
        else
        {
            log_error(c);
            rv = 1;
        }
    }
    else
    {
        ss_dassert(c.result == FW_ACTION_BLOCK);

        if (router_session.idle())
        {
            log_success(c);
        }
        else
        {
            router_session.discard_one_response();
            log_error(c);
            rv = 1;
        }
    }

    return rv;
}

int test(FilterModule::Instance& filter_instance, const FW_TEST& t)
{
    int rv = 0;

    mock::OkBackend backend;
    mock::RouterSession router_session(&backend);

    for (size_t i = 0; i < N_MAX_CASES; ++i)
    {
        const FW_TEST_CASE& c = t.cases[i];

        if (c.zStatement)
        {
            mock::Upstream upstream;
            mock::Session session(c.zUser, c.zHost, &upstream);

            auto_ptr<FilterModule::Session> sFilter_session = filter_instance.newSession(&session);

            if (sFilter_session.get())
            {
                router_session.set_as_downstream_on(sFilter_session.get());

                upstream.set_as_upstream_on(*sFilter_session.get());

                rv += test(*sFilter_session.get(), router_session, c);
            }
            else
            {
                ++rv;
            }
        }
        else
        {
            break;
        }
    }

    return rv;
}

int test(FilterModule& filter_module)
{
    int rv = 0;

    for (size_t i = 0; i < N_FIREWALL_TESTS; ++i)
    {
        const FW_TEST& t = FIREWALL_TESTS[i];

        const char* zAction = (t.action == FW_ACTION_ALLOW) ? "allow" : "block";

        cout << "ACTION: " << zAction << endl;
        cout << "RULES :\n" << t.zRules << endl;

        TempFile file;
        file.write(t.zRules);

        MXS_CONFIG_PARAMETER action { (char*)"action", (char*)zAction, NULL };
        MXS_CONFIG_PARAMETER rules = { (char*)"rules", (char*)file.name().c_str(), &action };

        auto_ptr<FilterModule::Instance> sInstance = filter_module.createInstance("test", NULL, &rules);

        if (sInstance.get())
        {
            rv += test(*sInstance.get(), t);
        }
        else
        {
            ++rv;
        }

        cout << "---------\n" << endl;
    }

    return rv;
}

int run()
{
    int rc = EXIT_FAILURE;

    auto_ptr<FilterModule> sModule = FilterModule::load("dbfwfilter");

    if (sModule.get())
    {
        if (maxscale::Module<void>::process_init())
        {
            if (maxscale::Module<void>::thread_init())
            {
                rc = test(*sModule.get());

                maxscale::Module<void>::thread_finish();
            }
            else
            {
                cerr << "error: Could not perform thread initialization." << endl;
            }

            maxscale::Module<void>::process_finish();
        }
        else
        {
            cerr << "error: Could not perform process initialization." << endl;
        }
    }
    else
    {
        cerr << "error: Could not load filter module." << endl;
    }

    return rc;
}

}

int main(int argc, char* argv[])
{
    int rc = EXIT_FAILURE;

    if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_STDOUT))
    {
        if (qc_setup("qc_sqlite", QC_SQL_MODE_DEFAULT, NULL))
        {
            if (qc_process_init(QC_INIT_SELF))
            {
                rc = run();

                qc_process_end(QC_INIT_SELF);
            }
            else
            {
                cerr << "error: Could not initialize query classifier." << endl;
            }
        }
        else
        {
            cerr << "error: Could not setup query classifier." << endl;
        }
    }

    return rc;
}

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
#include "maxscale/mock/client.hh"
#include "tempfile.hh"

using namespace std;
using maxscale::FilterModule;
using maxscale::QueryClassifierModule;
namespace mock = maxscale::mock;

namespace
{

struct CONFIG
{
    bool stop_at_first_error;
} config =
{
    true, // stop_at_first_error
};

enum fw_action_t
{
    FW_ACTION_ALLOW,
    FW_ACTION_BLOCK
};

struct FW_TEST_CASE
{
    const char* zStatement; /* The statement to test. */
    fw_action_t result;     /* The expected outcome. */
    const char* zUser;      /* The user to test as. */
    const char* zHost;      /* The host of the user. */
};

const char DEFAULT_USER[] = "bob";
const char DEFAULT_HOST[] = "127.0.0.1";

const size_t N_MAX_CASES = 20;

struct FW_TEST
{
    const char*  zRules;             /* The firewall rules. */
    fw_action_t  action;             /* The firewall action. */
    FW_TEST_CASE cases[N_MAX_CASES]; /* The test cases to execute using the above settings. */
};

FW_TEST FIREWALL_TESTS[] =
{
    //
    // wildcard
    //
    {
        "rule wildcard_used match wildcard\n"
        "users %@127.0.0.1 match any rules wildcard_used\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT * FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT * FROM t",
                FW_ACTION_ALLOW,
                DEFAULT_USER,
                "allowed_host"
            },
            {
                "SELECT a FROM t",
                FW_ACTION_ALLOW
            }
        }
    },
    {
        "rule wildcard_used match wildcard\n"
        "users %@127.0.0.1 match any rules wildcard_used\n",
        FW_ACTION_ALLOW,
        {
            {
                "SELECT * FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT * FROM t",
                FW_ACTION_BLOCK,
                DEFAULT_USER,
                "allowed_host"
            },
            {
                "SELECT a FROM t",
                FW_ACTION_BLOCK
            }
        }
    },
    //
    // columns
    //
    {
        "rule specific_column match columns a\n"
        "users bob@% match any rules specific_column\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT a, b FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT b, a FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT length(a) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT b FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT a FROM t",
                FW_ACTION_ALLOW,
                "alice"
            }
        }
    },
    //
    // function
    //
    {
        "rule specific_function match function sum count\n"
        "users %@% match any rules specific_function\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT sum(a) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT length(a) FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT length(a), count(b) FROM t",
                FW_ACTION_BLOCK
            }
        }
    },
    {
        "rule specific_function match function sum count\n"
        "users %@% match any rules specific_function\n",
        FW_ACTION_ALLOW,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT sum(a) FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT length(a) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT length(a), count(b) FROM t",
                FW_ACTION_ALLOW
            }
        }
    },
    //
    // not_function
    //
    {
        "rule other_functions_than match not_function length <\n"
        "users bob@% match any rules other_functions_than\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT length(a) FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT a FROM t WHERE a < b",
                FW_ACTION_ALLOW
            },
            {
                "SELECT concat(a) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT * FROM t WHERE a > b",
                FW_ACTION_BLOCK
            },
        }
    },
    {
        "rule other_functions_than match not_function length <\n"
        "users bob@% match any rules other_functions_than\n",
        FW_ACTION_ALLOW,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT length(a) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT a FROM t WHERE a < b",
                FW_ACTION_BLOCK
            },
            {
                "SELECT concat(a) FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT * FROM t WHERE a > b",
                FW_ACTION_ALLOW
            },
        }
    },
    //
    // uses_function
    //
    {
        "rule specific_column_used_with_function match uses_function a b\n"
        "users bob@% match any rules specific_column_used_with_function\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT a b FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT length(a) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT length(b) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT length(c) FROM t",
                FW_ACTION_ALLOW
            },
        }
    },
    {
        "rule specific_column_used_with_function match uses_function a b\n"
        "users bob@% match any rules specific_column_used_with_function\n",
        FW_ACTION_ALLOW,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT a b FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT length(a) FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT length(b) FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT length(c) FROM t",
                FW_ACTION_BLOCK
            },
        }
    },
    //
    // function and columns
    //
    {
        "rule specific_columns_used_with_function match function concat columns a b\n"
        "users bob@% match any rules specific_columns_used_with_function\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT length(a) FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT concat(a) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT concat(c) FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT a, concat(b) FROM t",
                FW_ACTION_BLOCK
            },
        }
    },
    {
        "rule specific_columns_used_with_function match function concat columns a b\n"
        "users bob@% match any rules specific_columns_used_with_function\n",
        FW_ACTION_ALLOW,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT length(a) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT concat(a) FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT concat(c) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT a, concat(b) FROM t",
                FW_ACTION_ALLOW
            },
        }
    },
    //
    // not_function and columns
    //
    {
        "rule specific_columns_used_with_other_function match not_function length columns a b\n"
        "users bob@% match any rules specific_columns_used_with_other_function\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT length(a) FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT concat(a) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT concat(c) FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT a, concat(b) FROM t",
                FW_ACTION_BLOCK
            },
        }
    },
    {
        "rule specific_columns_used_with_other_function match not_function length columns a b\n"
        "users bob@% match any rules specific_columns_used_with_other_function\n",
        FW_ACTION_ALLOW,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT length(a) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT concat(a) FROM t",
                FW_ACTION_ALLOW
            },
            {
                "SELECT concat(c) FROM t",
                FW_ACTION_BLOCK
            },
            {
                "SELECT a, concat(b) FROM t",
                FW_ACTION_ALLOW
            },
        }
    },
    //
    // regex
    //
    {
        "rule regex_match match regex '(?i).*select.*from.*account.*'\n"
        "users bob@% match any rules regex_match\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_ALLOW
            },
            {
                "select * FROM accounts",
                FW_ACTION_BLOCK
            }
        }
    },
    {
        "rule regex_match match regex '(?i).*select.*from.*account.*'\n"
        "users bob@% match any rules regex_match\n",
        FW_ACTION_ALLOW,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_BLOCK
            },
            {
                "select * FROM accounts",
                FW_ACTION_ALLOW
            }
        }
    },
    //
    // no_where_clause
    //
    {
        "rule rule1 match no_where_clause\n"
        "users bob@% match any rules rule1\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_BLOCK,
            },
            {
                "SELECT a FROM t WHERE b > c",
                FW_ACTION_ALLOW
            },
            {
                "DELETE FROM t",
                FW_ACTION_BLOCK,
            },
            {
                "DELETE FROM t WHERE a < b",
                FW_ACTION_ALLOW,
            }
        }
    },
    {
        "rule rule1 match no_where_clause\n"
        "users bob@% match any rules rule1\n",
        FW_ACTION_ALLOW,
        {
            {
                "SELECT a FROM t",
                FW_ACTION_ALLOW,
            },
            {
                "SELECT a FROM t WHERE b > c",
                FW_ACTION_BLOCK
            },
            {
                "DELETE FROM t",
                FW_ACTION_ALLOW,
            },
            {
                "DELETE FROM t WHERE a < b",
                FW_ACTION_BLOCK,
            }
        }
    },
    //
    // on_queries (some)
    //
    {
        "rule rule1 match regex '(?i).*xyz.*' on_queries select|delete|drop\n"
        "users bob@% match any rules rule1\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT xyz FROM t",
                FW_ACTION_BLOCK
            },
            {
                "INSERT INTO xyz VALUES (1)",
                FW_ACTION_ALLOW
            },
            {
                "UPDATE xyz SET a = 1",
                FW_ACTION_ALLOW,
            },
            {
                "DELETE FROM xyz",
                FW_ACTION_BLOCK
            },
            {
                "GRANT SELECT ON *.* TO 'xyz'@'localhost'",
                FW_ACTION_ALLOW
            },
            {
                "REVOKE INSERT ON *.* FROM 'xyz'@'localhost'",
                FW_ACTION_ALLOW
            },
            {
                "CREATE TABLE xyz (a INT)",
                FW_ACTION_ALLOW
            },
            {
                "ALTER TABLE xyz ADD (b INT)",
                FW_ACTION_ALLOW
            },
            {
                "DROP TABLE xyz",
                FW_ACTION_BLOCK
            },
            {
                "USE xyz",
                FW_ACTION_ALLOW
            },
            {
                "LOAD DATA INFILE 'data.txt' INTO TABLE db.xyz",
                FW_ACTION_ALLOW
            },
        }
    },
    //
    // any
    //
    {
        "rule rule1 match columns a\n"
        "rule rule2 match columns b\n"
        "rule rule3 match function length\n"
        "users bob@% match any rules rule1 rule2 rule3\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT a FROM t\n",
                FW_ACTION_BLOCK
            },
            {
                "SELECT b FROM t\n",
                FW_ACTION_BLOCK
            },
            {
                "SELECT length(c) FROM t\n",
                FW_ACTION_BLOCK
            },
        }
    },
    //
    // all
    //
    {
        "rule rule1 match columns a\n"
        "rule rule2 match columns b\n"
        "rule rule3 match function length\n"
        "users bob@% match all rules rule1 rule2 rule3\n",
        FW_ACTION_BLOCK,
        {
            {
                "SELECT a FROM t\n",
                FW_ACTION_ALLOW
            },
            {
                "SELECT b FROM t\n",
                FW_ACTION_ALLOW
            },
            {
                "SELECT length(c) FROM t\n",
                FW_ACTION_ALLOW
            },
            {
                "SELECT a, length(c) FROM t\n",
                FW_ACTION_ALLOW
            },
            {
                "SELECT a, b, length(c) FROM t\n",
                FW_ACTION_BLOCK
            },
        }
    }
};

const size_t N_FIREWALL_TESTS = sizeof(FIREWALL_TESTS) / sizeof(FIREWALL_TESTS[0]);

FW_TEST ON_QUERIES_TEST =
{
    "rule rule1 match regex '.*' on_queries %s\n"
    "users bob@%% match any rules rule1\n",
    FW_ACTION_BLOCK,
    {
        {
            "SELECT a FROM t",
            FW_ACTION_BLOCK
        },
        {
            "INSERT INTO t VALUES (1)",
            FW_ACTION_ALLOW
        },
        {
            "UPDATE t SET a = 1",
            FW_ACTION_ALLOW,
        },
        {
            "DELETE FROM a",
            FW_ACTION_ALLOW
        },
        {
            "GRANT SELECT ON *.* TO 'skysql'@'localhost'",
            FW_ACTION_ALLOW
        },
        {
            "REVOKE INSERT ON *.* FROM 'jeffrey'@'localhost'",
            FW_ACTION_ALLOW
        },
        {
            "CREATE TABLE t (a INT)",
            FW_ACTION_ALLOW
        },
        {
            "ALTER TABLE t ADD (b INT)",
            FW_ACTION_ALLOW
        },
        {
            "DROP TABLE t",
            FW_ACTION_ALLOW
        },
        {
            "USE d",
            FW_ACTION_ALLOW
        },
        {
            "LOAD DATA INFILE 'data.txt' INTO TABLE db.table",
            FW_ACTION_ALLOW
        },
    }
};

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

int test(mock::Client& client,
         FilterModule::Session& filter_session,
         mock::RouterSession& router_session,
         const FW_TEST_CASE& c)
{
    int rv = 0;

    cout << "STATEMENT: " << c.zStatement << endl;
    cout << "CLIENT   : " << client.user() << "@" << client.host() << endl;

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

    cout << endl;

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
            const char* zUser = c.zUser ? c.zUser : DEFAULT_USER;
            const char* zHost = c.zHost ? c.zHost : DEFAULT_HOST;

            mock::Client client(zUser, zHost);
            mock::Session session(&client);

            auto_ptr<FilterModule::Session> sFilter_session = filter_instance.newSession(&session);

            if (sFilter_session.get())
            {
                router_session.set_as_downstream_on(sFilter_session.get());

                client.set_as_upstream_on(*sFilter_session.get());

                rv += test(client, *sFilter_session.get(), router_session, c);
            }
            else
            {
                ++rv;
            }

            if ((rv != 0) && config.stop_at_first_error)
            {
                break;
            }
        }
        else
        {
            // No more test cases.
            break;
        }
    }

    return rv;
}

int test(FilterModule& filter_module, const FW_TEST& t)
{
    int rv = 0;

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

    return rv;
}

int test(FilterModule& filter_module)
{
    int rv = 0;

    for (size_t i = 0; i < N_FIREWALL_TESTS; ++i)
    {
        const FW_TEST& t = FIREWALL_TESTS[i];

        rv += test(filter_module, t);

        if ((rv != 0) && config.stop_at_first_error)
        {
            break;
        }
    }

    return rv;
}

int test_on_queries(FilterModule& filter_module, fw_action_t action)
{
    int rv = 0;

    static const char* OPERATIONS[] =
    {
        "select", "insert", "update", "delete", "grant", "revoke", "create", "alter", "drop", "use", "load"
    };
    static const size_t N_OPERATIONS = sizeof(OPERATIONS) / sizeof(OPERATIONS[0]);

    FW_TEST t = ON_QUERIES_TEST;

    t.action = action;
    fw_action_t result_match = action;
    fw_action_t result_not_match = (action == FW_ACTION_BLOCK) ? FW_ACTION_ALLOW : FW_ACTION_BLOCK;

    char rules[strlen(t.zRules) + 32]; // Enough
    const char* zFormat = t.zRules;

    for (size_t i = 0; i < N_OPERATIONS; ++i)
    {
        FW_TEST_CASE& c = t.cases[i];

        sprintf(rules, zFormat, OPERATIONS[i]);

        t.zRules = rules;

        for (size_t j = 0; j < N_OPERATIONS; ++j)
        {
            if (j == i)
            {
                t.cases[j].result = result_match;
            }
            else
            {
                t.cases[j].result = result_not_match;
            }
        }

        rv += test(filter_module, t);

        if ((rv != 0) && config.stop_at_first_error)
        {
            break;
        }
    }

    t.zRules = zFormat;

    return rv;
}

int test_on_queries(FilterModule& filter_module)
{
    int rv = 0;

    rv += test_on_queries(filter_module, FW_ACTION_BLOCK);

    if ((rv == 0) && !config.stop_at_first_error)
    {
        rv += test_on_queries(filter_module, FW_ACTION_ALLOW);
    }

    return rv;
}

int run()
{
    int rv = 1;

    auto_ptr<FilterModule> sModule = FilterModule::load("dbfwfilter");

    if (sModule.get())
    {
        if (maxscale::Module::process_init())
        {
            if (maxscale::Module::thread_init())
            {
                rv = 0;
                rv += test(*sModule.get());

                if ((rv == 0) || !config.stop_at_first_error)
                {
                    rv += test_on_queries(*sModule.get());
                }

                maxscale::Module::thread_finish();
            }
            else
            {
                cerr << "error: Could not perform thread initialization." << endl;
            }

            maxscale::Module::process_finish();
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

    return rv;
}

}

namespace
{

char USAGE[] =
    "usage: test_dbfwfilter [-d]\n"
    "\n"
    "-d    don't stop at first error\n";
}

int main(int argc, char* argv[])
{
    int rv = 0;

    int c;
    while ((c = getopt(argc, argv, "d")) != -1)
    {
        switch (c)
        {
        case 'd':
            config.stop_at_first_error = false;
            break;

        default:
            rv = 1;
        }
    }

    if (rv == 0)
    {
        if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_STDOUT))
        {
            if (qc_setup("qc_sqlite", QC_SQL_MODE_DEFAULT, NULL))
            {
                if (qc_process_init(QC_INIT_SELF))
                {
                    rv = run();

                    cout << rv << " failures." << endl;

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
    }
    else
    {
        cout << USAGE << endl;
    }

    return rv;
}

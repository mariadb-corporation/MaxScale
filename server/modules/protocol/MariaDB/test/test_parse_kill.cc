/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/client_connection.hh>

#include <inttypes.h>
#include <string>
#include <maxbase/alloc.h>
#include <maxbase/format.hh>

using kill_type_t = MariaDBClientConnection::kill_type_t;
constexpr auto KT_HARD = MariaDBClientConnection::KT_HARD;
constexpr auto KT_SOFT = MariaDBClientConnection::KT_SOFT;
constexpr auto KT_CONNECTION = MariaDBClientConnection::KT_CONNECTION;
constexpr auto KT_QUERY = MariaDBClientConnection::KT_QUERY;

struct test_t
{
    std::string query;
    bool        should_succeed {false};
    uint64_t    correct_id {0};
    uint32_t    correct_kt {0};
    std::string correct_user;
};

int test_one_query(test_t test)
{
    auto& sql = test.query;
    auto len = sql.length();

    MariaDBClientConnection::KillQueryContents kill_contents;
    bool is_kill_query = false;

    // This part should closely match MariaDBClientConnection::process_special_queries so that the regex
    // is used properly.
    const auto& regex = MariaDBClientConnection::special_queries_regex();
    auto found = regex.match(sql.c_str(), len);
    if (found)
    {
        auto main_ind = regex.substring_ind_by_name("main");
        mxb_assert(!main_ind.empty());
        char c = sql[main_ind.begin];
        switch (c)
        {
        case 'K':
        case 'k':
            {
                is_kill_query = true;
                kill_contents = MariaDBClientConnection::parse_kill_query_elems(sql.c_str());
            }
            break;

        default:
            break;
        }
    }

    std::string errmsg;
    if (is_kill_query != test.should_succeed)
    {
        errmsg = mxb::string_printf("Expected success '%d', got '%d'", test.should_succeed, is_kill_query);
    }
    else if (kill_contents.kt != test.correct_kt)
    {
        errmsg = mxb::string_printf("Expected kill type '%u', got '%u'", test.correct_kt, kill_contents.kt);
    }
    else if (kill_contents.id != test.correct_id)
    {
        errmsg = mxb::string_printf("Expected thread id '%lu', got '%lu'", test.correct_id, kill_contents.id);
    }
    else if (kill_contents.user != test.correct_user)
    {
        errmsg = mxb::string_printf("Expected user '%s', got '%s'",
                                    test.correct_user.c_str(), kill_contents.user.c_str());
    }
    if (errmsg.empty())
    {
        return 0;
    }
    else
    {
        printf("Result wrong on query: '%s': %s.\n", sql.c_str(), errmsg.c_str());
        return 1;
    }
}

int main(int argc, char** argv)
{
    mxs_log_init(NULL, ".", MXS_LOG_TARGET_STDOUT);
    MariaDBClientConnection::module_init();

    /*
     * The second column is true for cases where the query matches the regex, but reading the id fails due
     * to overflow. In these cases the third col is 0, as the parser returns that by default. 0 is not a
     * valid connection id.
     *
     * Also, the regex does not check the remainder of the query. This could be added, but would cause
     * comments to reject an otherwise fine kill-query. This means that currently, "kill connection 123 HARD"
     * is interpreted as "kill connection 123".
     */

    test_t tests[] =
    {
        {" kill ConNectioN 123  ",                     true,  123,          KT_CONNECTION          },
        {"kIlL  coNNectioN 987654321  ;",              true,  987654321,    KT_CONNECTION          },
        {" Ki5L CoNNectioN 987654321  ",               false,                                      },
        {"1",                                          false,                                      },
        {"kILL 1",                                     true,  1,                                   },
        {"\n\t kill \nQueRy 456",                      true,  456,          KT_QUERY               },
        {"     A         kill 1;     ",                false,                                      },
        {" kill connection 1A",                        false,                                      },
        {" kill connection 1 A ",                      true,  1,            KT_CONNECTION          },
        {"kill query 7 ; select * ",                   true,  7,            KT_QUERY               },
        // 64-bit integer overflow
        {"KIll query 123456789012345678901",           true,  0,            KT_QUERY               },
        {"KIll query   \t    \t   21  \n \t  ",        true,  21,           KT_QUERY               },
        {"KIll   \t    \n    \t   -6  \n \t   ",       false,                                      },
        {"KIll 12345678901234567890123456 \n \t",      true,                                       },
        {"kill ;",                                     false, 0,                                   },
        {" kill ConNectioN 123 HARD",                  true, 123,           KT_CONNECTION          },
        {" kill ConNectioN 123 SOFT",                  true, 123,           KT_CONNECTION          },
        {" kill ConNectioN SOFT 123",                  false, 0,                                   },
        {"           kill  HARD ConNectioN 123",       true,  123,          KT_CONNECTION | KT_HARD},
        {" kill  SOFT ConNectioN 123",                 true,  123,          KT_CONNECTION | KT_SOFT},
        {" kill  HARD 123",                            true,  123,          KT_HARD                },
        {" kill  SOFT 123",                            true,  123,          KT_SOFT                },
        {"KIll soft query 21 ",                        true,  21,           KT_QUERY | KT_SOFT     },
        {"KIll query soft 21 ",                        false,                                      },
        {"KIll query user maxuser ",                   true, 0,             KT_QUERY, "maxuser"    },
        {"KIll user               ",                   false,                                      }
    };
    int result = 0;
    int arr_size = sizeof(tests) / sizeof(test_t);
    for (int i = 0; i < arr_size; i++)
    {
        result += test_one_query(tests[i]);
    }
    return result;
}

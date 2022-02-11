/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/client_connection.hh>
#include "../detect_special_query.hh"
#include <inttypes.h>
#include <string>
#include <maxbase/alloc.h>
#include <maxbase/format.hh>

using kill_type_t = MariaDBClientConnection::kill_type_t;
constexpr auto KT_HARD = MariaDBClientConnection::KT_HARD;
constexpr auto KT_SOFT = MariaDBClientConnection::KT_SOFT;
constexpr auto KT_CONNECTION = MariaDBClientConnection::KT_CONNECTION;
constexpr auto KT_QUERY = MariaDBClientConnection::KT_QUERY;
using Type = MariaDBClientConnection::SpecialQueryDesc::Type;

struct test_t
{
    std::string query;
    Type        type {Type::NONE};
    uint64_t    correct_id {0};
    uint32_t    correct_kt {0};
    std::string correct_target;
};

int test_one_query(const test_t& test)
{
    MariaDBClientConnection::SpecialQueryDesc query_desc;

    const char* pSql = test.query.data();
    const char* pEnd = pSql + test.query.size();
    bool is_special = detect_special_query(&pSql, pEnd);
    if (is_special)
    {
        query_desc = MariaDBClientConnection::parse_special_query(pSql, pEnd - pSql);
    }

    auto found_type = query_desc.type;
    auto found_kt = query_desc.kill_options;
    auto found_id = query_desc.kill_id;
    auto& found_target = query_desc.target;

    std::string errmsg;
    if (found_type != test.type)
    {
        errmsg = mxb::string_printf("Expected type '%i', got '%i'", (int)test.type, (int)found_type);
    }
    else if (found_kt != test.correct_kt)
    {
        errmsg = mxb::string_printf("Expected kill type '%u', got '%u'", test.correct_kt, found_kt);
    }
    else if (found_id != test.correct_id)
    {
        errmsg = mxb::string_printf("Expected thread id '%lu', got '%lu'", test.correct_id, found_id);
    }
    else if (found_target != test.correct_target)
    {
        errmsg = mxb::string_printf("Expected target '%s', got '%s'",
                                    test.correct_target.c_str(), found_target.c_str());
    }
    if (errmsg.empty())
    {
        return 0;
    }
    else
    {
        printf("Result wrong on query: '%s': %s.\n", test.query.c_str(), errmsg.c_str());
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
     */

    auto KILL = Type::KILL;
    auto NONE = Type::NONE;
    auto ROLE = Type::SET_ROLE;
    auto DB = Type::USE_DB;

    test_t tests[] =
    {
        {" kill ConNectioN 123  ",                KILL,  123,       KT_CONNECTION          },
        {"kIlL  coNNectioN 987654321  ;",         KILL,  987654321, KT_CONNECTION          },
        {" Ki5L CoNNectioN 987654321  ",          NONE, },
        {"1",                                     NONE, },
        {"kILL 1  ;",                             KILL,  1,                                },
        {"\n\t kill \nQueRy 456",                 KILL,  456,       KT_QUERY               },
        {"     A         kill 1;     ",           NONE, },
        {" kill connection 1A",                   NONE, },
        {" kill connection 1 A ",                 NONE, },
        {"kill query 7 ; select * ",              KILL,  7,         KT_QUERY               },
        // 64-bit integer overflow
        {"KIll query 123456789012345678901",      KILL,  0,         KT_QUERY               },
        {"KIll query   \t    \t   21  \n \t  ",   KILL,  21,        KT_QUERY               },
        {"KIll   \t    \n    \t   -6  \n \t   ",  NONE, },
        {"KIll 12345678901234567890123456 \n \t", KILL,  },
        {"kill ;",                                NONE, 0,                                },
        {" kill ConNectioN 123 HARD",             NONE, 0},
        {" kill ConNectioN SOFT 123",             NONE, 0,                                },
        {"/* \ncomment1\ncomment2*/         kill  HARD ConNectioN 123",
         KILL,  123,       KT_CONNECTION | KT_HARD},
        {"/*** star* *comm///*EnT ****/  \n--linecomment\n  /***/kill 123",
         KILL,  123,       },
        {"#line-comment\nkill  SOFT ConNectioN 123",
         KILL,  123,       KT_CONNECTION | KT_SOFT},
        {"-- line comment USE test;\n #set role my_role\n   kill  HARD 123",
         KILL,  123,       KT_HARD                },
        {" kill  SOFT 123",                       KILL, 123,       KT_SOFT                },
        {"KIll soft query 21 ",                   KILL, 21,        KT_QUERY | KT_SOFT     },
        {"KIll query soft 21 ",                   NONE, },
        {"KIll query user maxuser ",              KILL, 0,         KT_QUERY, "maxuser"    },
        {"KIll user               ",              NONE, },
        {" #line-comment\n KILL 2 /* ab */    ",  KILL, 2},
        {"KILL 42 \n --ab    ",                   KILL, 42},
        {"use ;",                                 NONE, 0,         0,                     },
        {"use db1;",                              DB,   0,         0,        "db1"        },
        {" SET  ASDF;",                           NONE, 0,         0,                     },
        {"/** comment */ seT  RolE  my_role ;",   ROLE, 0,         0,        "my_role"    },
    };

    int result = 0;
    for (auto& elem : tests)
    {
        result += test_one_query(elem);
    }
    return result;
}

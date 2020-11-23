/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#ifndef SS_DEBUG
#define SS_DEBUG
#endif

#include "../hintparser.cc"
#include "../hintfilter.cc"
#include <maxbase/log.hh>

#include <algorithm>
#include <iostream>
#include <initializer_list>

int errors = 0;

void test(const std::string& input, std::initializer_list<std::string> expected)
{
    bool rval = true;
    auto it = expected.begin();
    mxs::Buffer buffer(input.c_str(), input.size());

    for (auto output : get_all_comments(buffer.begin(), buffer.end()))
    {
        if (it == expected.end())
        {
            std::cout << "Too much output: " << std::string(output.first, output.second) << std::endl;
            errors++;
            break;
        }

        int have = std::distance(output.first, output.second);
        int need = it->length();

        if (have != need)
        {
            std::cout << "Need " << need << " bytes but only have " << have << std::endl;
            errors++;
        }
        else if (!std::equal(output.first, output.second, it->begin()))
        {
            std::cout << "Output not equal to expected output" << std::endl;
            errors++;
        }

        if (!rval)
        {
            std::cout << "Input: " << input << std::endl;
            std::cout << "Output: " << std::string(output.first, output.second) << std::endl;
            std::cout << "Expected: " << *it << std::endl;
            std::cout << std::endl;
        }

        ++it;
    }

    if (it != expected.end())
    {
        std::cout << "Not enough output, need " << std::distance(it, expected.end())
                  << " more comments" << std::endl;
        errors++;
    }
}


void test_parse(const std::string& input, int expected_type)
{
    mxs::Buffer buffer(input.c_str(), input.size());
    HintParser parser;
    HINT* hint = parser.parse(buffer.begin(), buffer.end());

    if (!hint && expected_type != 0)
    {
        std::cout << "Expected hint but didn't get one: " << input << std::endl;
        errors++;
    }
    else if (hint && expected_type == 0)
    {
        std::cout << "Expected no hint but got one: " << input << std::endl;
        errors++;
    }
    else if (hint && hint->type != expected_type)
    {
        std::cout << "Expected hint of type " << expected_type << " but got type "
                  << (int)hint->type << ": " << input << std::endl;
        errors++;
    }

    hint_free(hint);
}

void count_hints(const std::string& input, int num_expected)
{
    mxs::Buffer buffer(input.c_str(), input.size());
    HintParser parser;
    int n = 0;
    HINT* hint = parser.parse(buffer.begin(), buffer.end());

    for (; hint; hint = hint->next)
    {
        n++;
    }

    if (n != num_expected)
    {
        std::cout << "Expected " << num_expected << " hints but have " << n << ":" << input << std::endl;
        errors++;
    }

    hint_free(hint);
}

int main(int argc, char** argv)
{
    mxb::Log log(MXB_LOG_TARGET_STDOUT);

    // Simple comments
    test("select 1 -- this is a comment", {"this is a comment"});
    test("select 1 #this is a comment", {"this is a comment"});
    test("select 1 # this is a comment", {" this is a comment"});
    test("select 1 /*this is a comment*/", {"this is a comment"});

    // Comments on line before, after and in between queries
    test("-- this is a comment\nselect 1", {"this is a comment"});
    test("#this is a comment\nselect 1", {"this is a comment"});
    test("select 1\n-- this is a comment", {"this is a comment"});
    test("select 1\n#this is a comment", {"this is a comment"});
    test("select 1;\n-- this is a comment\nselect 2;", {"this is a comment"});
    test("select 1;\n#this is a comment\nselect 2;", {"this is a comment"});

    // Comment blocks on multiple lines
    test("select 1\n/* this is a comment */", {" this is a comment "});
    test("select 1\n/*this is \na comment*/", {"this is \na comment"});
    test("select 1\n/**\n *this is \n* a comment\n*/", {"*\n *this is \n* a comment\n"});
    test("select /*this is a comment*/ 1", {"this is a comment"});
    test("select 1\n/* this is \na comment */", {" this is \na comment "});

    // Multiple comments in the same query
    test("select /*first*/ 1 /*second*/", {"first", "second"});
    test("-- first\nselect 1\n-- second", {"first", "second"});
    test("/** first comment */ select 1 -- second comment", {"* first comment ", "second comment"});
    test("#first\nselect 1\n#second#comment", {"first", "second#comment"});
    test("#first\nselect 1/*second*/-- third", {"first", "second", "third"});

    // Comments inside quotes
    test("select '/*do not parse this*/' /*parse this*/", {"parse this"});
    test("select \"/*do not parse this*/\" /*parse this*/", {"parse this"});
    test("select `/*do not parse this*/`/*parse this*/", {"parse this"});
    test("select/*parse this*/ '/*do not parse this*/'", {"parse this"});
    test("select/*parse this*/ \"/*do not parse this*/\"", {"parse this"});
    test("select/*parse this*/ `/*do not parse this*/`", {"parse this"});
    test("select \"/*do not\\\" parse this*/\"", {});
    test("select '/*do not'' parse this*/'", {});
    test("select '/*do not\\' parse this*/'", {});

    // Malformed input
    test("select '/*do not parse this*/\"", {});
    test("select \"/*do not parse this*/'", {});
    test("select `/*do not parse this*/'", {});
    test("select `/*do not parse this*/\"", {});
    test("select \"/*do not parse this*/", {});
    test("select '/*do not parse this*/", {});
    test("select `/*do not parse this*/", {});
    test("select /do not parse this*/", {});
    test("select / *do not parse this*/", {});
    test("select /*do not parse this* /", {});
    test("select /*do not parse this*\\/", {});
    test("select /\n*do not parse this*/", {});
    test("select --\ndo not parse this", {});
    test("select --\tdo not parse this", {});
    test("select ' \\' -- do not parse this", {});
    test("select \" \\\" -- do not parse this", {});
    test("select ` \\` -- do not parse this", {});

    // MXS-2289
    test("select 1; --bad comment", {});
    test("select 1; --bad comment\n -- working comment", {"working comment"});
    test("-- working comment\nselect 1; --bad comment", {"working comment"});
    test("select 1 -- working comment --bad comment", {"working comment --bad comment"});

    test_parse("SELECT 1 /* maxscale route to master */", HINT_ROUTE_TO_MASTER);
    test_parse("SELECT 1 /* maxscale route to slave */", HINT_ROUTE_TO_SLAVE);
    test_parse("SELECT 1 /* maxscale route to last*/", HINT_ROUTE_TO_LAST_USED);
    test_parse("SELECT 1 /* maxscale route to server server1 */", HINT_ROUTE_TO_NAMED_SERVER);
    test_parse("SELECT 1 /* maxscale test1 prepare route to server server1 */", 0);
    test_parse("SELECT 1 /* maxscale test1 start route to server server1 */", HINT_ROUTE_TO_NAMED_SERVER);
    test_parse("SELECT 1 /* maxscale start route to server server1 */", HINT_ROUTE_TO_NAMED_SERVER);
    test_parse("SELECT 1 /* maxscale end*/", 0);
    test_parse("SELECT 1 /* maxscale end*/", 0);
    test_parse("SELECT 1 /* maxscale key=value */", HINT_PARAMETER);
    test_parse("SELECT 1 /* maxscale max_slave_replication_lag=1*/", HINT_PARAMETER);

    // Process multiple comments  with hints in them
    // Note: How the hints are used depends on the router module
    count_hints("SELECT /* comment before hint */ 1 /* maxscale route to master */", 1);
    count_hints("SELECT /* maxscale route to master */1/* comment after hint */", 1);
    count_hints("SELECT /* maxscale route to slave */ 1 /* maxscale route to master */", 2);
    count_hints("#maxscale route to slave\nSELECT 1;\n#maxscale route to master", 2);
    count_hints("-- maxscale route to slave\nSELECT 1;\n-- maxscale route to master", 2);
    count_hints("#maxscale route to slave \n#comment after hint\nSELECT 1", 1);
    count_hints("#comment before hint\n#maxscale route to slave \nSELECT 1", 1);

    // Hints with unexpected trailing input and unknown input
    count_hints("/* maxscale route to slave server */ SELECT 1", 0);
    count_hints("/* maxscale route to something */ SELECT 1", 0);
    count_hints("/* maxscale route master */ SELECT 1", 0);
    count_hints("/* maxscale route slave */ SELECT 1", 0);
    count_hints("/* maxscale route to slave \n# comment inside comment\n */ SELECT 1", 0);
    count_hints("/* maxscale route to slave -- */ SELECT 1", 0);
    count_hints("/* maxscale route to slave # */ SELECT 1", 0);
    count_hints("#/* maxscale route to slave */ SELECT 1", 0);
    count_hints("-- /* maxscale route to slave */ SELECT 1", 0);
    count_hints("-- # maxscale route to slave */ SELECT 1", 0);
    count_hints("#-- maxscale route to slave */ SELECT 1", 0);

    // The extra asterisk is a part of the comment and should cause the hint to be ignored. It could be
    // processed but for the sake of simplicity and "bug compatibility" with 2.3 it is treated as an error.
    count_hints("/**maxscale route to slave*/ SELECT 1", 0);

    return errors;
}

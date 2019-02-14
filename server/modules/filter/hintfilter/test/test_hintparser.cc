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

#ifndef SS_DEBUG
#define SS_DEBUG
#endif

#include "../hintparser.cc"
#include <maxbase/log.hh>

#include <algorithm>
#include <iostream>
#include <initializer_list>

void test(const std::string& input, std::initializer_list<std::string> expected)
{
    bool rval = true;
    auto it = expected.begin();

    for (auto output : get_all_comments(input.begin(), input.end()))
    {
        if (it == expected.end())
        {
            std::cout << "Too much output: " << std::string(output.first, output.second) << std::endl;
            rval = false;
            break;
        }

        int have = std::distance(output.first, output.second);
        int need = it->length();

        if (have != need)
        {
            std::cout << "Need " << need << " bytes but only have " << have << std::endl;
            rval = false;
        }
        else if (!std::equal(output.first, output.second, it->begin()))
        {
            std::cout << "Output not equal to expected output" << std::endl;
            rval = false;
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
        rval = false;
    }

    mxb_assert(rval);
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

    return 0;
}

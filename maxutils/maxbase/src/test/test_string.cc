/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/string.hh>
#include <maxbase/assert.hh>
#include <string.h>
#include <iostream>
#include <tuple>

using std::cout;
using std::endl;

namespace
{

#define TRIM_TCE(zFrom, zTo) {zFrom, zTo}

struct TRIM_TEST_CASE
{
    const char* zFrom;
    const char* zTo;
};

TRIM_TEST_CASE trim_testcases[] =
{
    TRIM_TCE("",        ""),
    TRIM_TCE("a",       "a"),
    TRIM_TCE(" a",      "a"),
    TRIM_TCE("a ",      "a"),
    TRIM_TCE(" a ",     "a"),
    TRIM_TCE("  a",     "a"),
    TRIM_TCE("a  ",     "a"),
    TRIM_TCE("  a  ",   "a"),
    TRIM_TCE("  a b  ", "a b"),
};

const int n_trim_testcases = sizeof(trim_testcases) / sizeof(trim_testcases[0]);

TRIM_TEST_CASE ltrim_testcases[] =
{
    TRIM_TCE("",        ""),
    TRIM_TCE("a",       "a"),
    TRIM_TCE(" a",      "a"),
    TRIM_TCE("a ",      "a "),
    TRIM_TCE(" a ",     "a "),
    TRIM_TCE("  a",     "a"),
    TRIM_TCE("a  ",     "a  "),
    TRIM_TCE("  a  ",   "a  "),
    TRIM_TCE("  a b  ", "a b  "),
};

const int n_ltrim_testcases = sizeof(ltrim_testcases) / sizeof(ltrim_testcases[0]);

TRIM_TEST_CASE rtrim_testcases[] =
{
    TRIM_TCE("",        ""),
    TRIM_TCE("a",       "a"),
    TRIM_TCE(" a",      " a"),
    TRIM_TCE("a ",      "a"),
    TRIM_TCE(" a ",     " a"),
    TRIM_TCE("  a",     "  a"),
    TRIM_TCE("a  ",     "a"),
    TRIM_TCE("  a  ",   "  a"),
    TRIM_TCE("  a b  ", "  a b"),
};

const int n_rtrim_testcases = sizeof(rtrim_testcases) / sizeof(rtrim_testcases[0]);


int test(TRIM_TEST_CASE* pTest_cases, int n_test_cases, char* (*p)(char*))
{
    int rv = 0;

    for (int i = 0; i < n_test_cases; ++i)
    {
        const char* zFrom = pTest_cases[i].zFrom;
        const char* zTo = pTest_cases[i].zTo;

        char copy[strlen(zFrom) + 1];
        strcpy(copy, zFrom);

        char* z = p(copy);

        if (strcmp(z, zTo) != 0)
        {
            ++rv;
        }
    }

    return rv;
}

int test_trim()
{
    cout << "trim()" << endl;
    return test(trim_testcases, n_trim_testcases, mxb::trim);
}

int test_ltrim()
{
    cout << "ltrim()" << endl;
    return test(ltrim_testcases, n_ltrim_testcases, mxb::ltrim);
}

int test_rtrim()
{
    cout << "rtrim()" << endl;
    return test(rtrim_testcases, n_rtrim_testcases, mxb::rtrim);
}

int test_split()
{
    cout << "split()" << endl;

    std::vector<std::tuple<std::string_view,
                           std::string_view,
                           std::string_view,
                           std::string_view>> test_cases
    {
        {"hello=world", "=", "hello", "world", },
        {"=world", "=", "", "world", },
        {"=world", "", "=world", ""},
        {"helloworld!", "!", "helloworld", ""},
        {"helloworld!", "=", "helloworld!", ""},
        {"helloworld!", "\0", "helloworld!", ""},
        {"hello world!", "  ", "hello world!", ""},
        {"hello world!", " ", "hello", "world!"},
        {"hello world!", "world", "hello ", "!"},
    };

    int rc = 0;

    for (const auto& [input, delim, head, tail] : test_cases)
    {
        if (auto [split_head, split_tail] =
                mxb::split(input, delim); head != split_head || tail != split_tail)
        {
            cout << "`" << input << "` with delimiter `" << delim << "` returned "
                 << "`" << split_head << "` and `" << split_tail << "` "
                 << "instead of `" << head << "` and `" << tail << "`" << endl;
            rc = 1;
        }
    }

    return rc;
}

int test_cat()
{
    cout << "cat()" << endl;
    int rc = 0;

    auto expect = [&](std::string result, auto expected){
        if (result != expected)
        {
            cout << "Expected '" << expected << "' got '" << result << "'";
            rc++;
        }
    };

    expect(mxb::cat("", ""), "");
    expect(mxb::cat("1"), "1");
    expect(mxb::cat("2", ""), "2");
    expect(mxb::cat("", "3"), "3");
    expect(mxb::cat("", "4", ""), "4");

    expect(mxb::cat("hello", "world"), "helloworld");
    expect(mxb::cat(std::string("hello"), "world"), "helloworld");
    expect(mxb::cat(std::string_view("hello"), "world"), "helloworld");

    expect(mxb::cat("hello", "world"), "helloworld");
    expect(mxb::cat("hello", std::string("world")), "helloworld");
    expect(mxb::cat("hello", std::string_view("world")), "helloworld");

    expect(mxb::cat(std::string_view("hello"), "world"), "helloworld");
    expect(mxb::cat(std::string_view("hello"), std::string("world")), "helloworld");
    expect(mxb::cat(std::string_view("hello"), std::string_view("world")), "helloworld");

    std::string str = "std::string";
    std::string_view sv = "std::string_view";
    const char* cchar = "const char*";

    expect(mxb::cat(str), str);
    expect(mxb::cat(sv), sv);
    expect(mxb::cat(cchar), cchar);

    expect(mxb::cat(str, sv), str + std::string {sv});
    expect(mxb::cat(str, cchar), str + cchar);
    expect(mxb::cat(sv, str), std::string {sv} + str);
    expect(mxb::cat(sv, cchar), std::string {sv} + cchar);
    expect(mxb::cat(cchar, str), cchar + str);
    expect(mxb::cat(cchar, sv), cchar + std::string {sv});

    return rc;
}
}

int main(int argc, char* argv[])
{
    int rv = 0;

    rv += test_trim();
    rv += test_ltrim();
    rv += test_rtrim();
    rv += test_split();
    rv += test_cat();

    return rv;
}

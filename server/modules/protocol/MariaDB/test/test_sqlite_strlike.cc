/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */


#include "../sqlite_strlike.hh"

enum CaseSetting {RESPECT, IGNORE};

struct Test
{
    const char* subject {nullptr};
    const char* pattern {nullptr};
    CaseSetting case_sett {RESPECT};
    bool        match {false};
};

int test_one(const Test& t);

int main(int argc, char** argv)
{
    Test tests[] = {
        {"A",                "a",                IGNORE,  true },
        {"A",                "a",                RESPECT, false},
        {"Bond, James Bond", "Bon_, James%Bond", RESPECT, true },
        {"Bond, James Bond", "Bon_, james%bond", RESPECT, false},
        {"Bond, James Bond", "Bon_, james%bond", IGNORE,  true },
        {"Bond, James Bond", "%d, _____ ____",   IGNORE,  true },
        {"Bond, James Bond", "%d, _____ _____",  IGNORE,  false},
        {"aabbccddeeffgg",   "aa%cc%ee%gg",      RESPECT, true },
        {"my_db",            "my_db",            RESPECT, true },
        {"my_db",            R"(my\_db)",        RESPECT, true },
        {"my1db",            R"(my_db)",         RESPECT, true },
        {"my1db",            R"(my\_db)",        RESPECT, false},
        {"mydb_test1",       R"(mydb_%)",        RESPECT, true },
        {"mydb_test1",       R"(mydb_\%)",       RESPECT, false},
        {"192.168.0.1",      "192.%.0.1",        IGNORE,  true },
        {"192.168.0.1",      "192.%.1.1",        IGNORE,  false},
        {"www.mArIaDb.com",  "www.Ma%dB.com",    IGNORE,  true },
        {nullptr}
    };

    int result = 0;
    int i = 0;
    while (tests[i].subject)
    {
        result += test_one(tests[i]);
        i++;
    }
    return result;
}

int test_one(const Test& t)
{
    int match_res = (t.case_sett == RESPECT) ? sql_strlike_case(t.pattern, t.subject, '\\') :
        sql_strlike(t.pattern, t.subject, '\\');
    bool matched = (match_res == 0);
    int rval = 1;
    if (matched == t.match)
    {
        rval = 0;
    }
    else
    {
        const char* expected_str = t.match ? "match" : "no-match";
        const char* found_str = matched ? "match" : "no-match";
        const char* case_str = (t.case_sett == RESPECT) ? "case-sensitive" : "case-insensitive";

        printf("Failure on subject '%s', pattern '%s', %s. Expected %s, got %s.\n",
               t.subject, t.pattern, case_str, expected_str, found_str);
    }
    return rval;
}

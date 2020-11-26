/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#ifndef SS_DEBUG
#define SS_DEBUG
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxbase/alloc.h>
#include <maxscale/pcre2.hh>
#include <maxbase/regex.hh>

#define test_assert(a, b) if (!(a)) {fprintf(stderr, b); return 1;}

/**
 * Test PCRE2 regular expression simple matching function test
 */
static int test1()
{
    int error = 0;
    mxs_pcre2_result_t result = mxs_pcre2_simple_match("brown.*dog",
                                                       "The quick brown fox jumps over the lazy dog",
                                                       0,
                                                       &error);
    test_assert(result == MXS_PCRE2_MATCH, "Pattern should match");
    error = 0;
    result = mxs_pcre2_simple_match("BROWN.*DOG",
                                    "The quick brown fox jumps over the lazy dog",
                                    PCRE2_CASELESS,
                                    &error);
    test_assert(result == MXS_PCRE2_MATCH, "Pattern should match with PCRE2_CASELESS option");
    error = 0;
    result = mxs_pcre2_simple_match("black.*dog", "The quick brown fox jumps over the lazy dog", 0, &error);
    test_assert(result == MXS_PCRE2_NOMATCH && error == 0, "Pattern should not match");
    error = 0;
    result = mxs_pcre2_simple_match("black.*[dog", "The quick brown fox jumps over the lazy dog", 0, &error);
    test_assert(result == MXS_PCRE2_ERROR, "Pattern should not match and a failure should be retured");
    test_assert(error != 0, "Error number should be non-zero");
    return 0;
}

/**
 * Test PCRE2 string substitution
 */
static int test2()
{
    int err;
    size_t erroff;
    const char* pattern = "(.*)dog";
    const char* pattern2 = "(.*)duck";
    const char* good_replace = "$1cat";
    const char* bad_replace = "$6cat";
    const char* subject = "The quick brown fox jumps over the lazy dog";
    const char* expected = "The quick brown fox jumps over the lazy cat";

    /** We'll assume malloc and the PCRE2 library works */
    pcre2_code* re = pcre2_compile((PCRE2_SPTR) pattern,
                                   PCRE2_ZERO_TERMINATED,
                                   0,
                                   &err,
                                   &erroff,
                                   NULL);
    pcre2_code* re2 = pcre2_compile((PCRE2_SPTR) pattern2,
                                    PCRE2_ZERO_TERMINATED,
                                    0,
                                    &err,
                                    &erroff,
                                    NULL);
    size_t size = 1000;
    char* dest = (char*)MXS_MALLOC(size);
    MXS_ABORT_IF_NULL(dest);
    mxs_pcre2_result_t result = mxs_pcre2_substitute(re, subject, good_replace, &dest, &size);

    test_assert(result == MXS_PCRE2_MATCH, "Substitution should substitute");
    test_assert(strcmp(dest, expected) == 0, "Replaced text should match expected text");

    size = 1000;
    dest = (char*)MXS_REALLOC(dest, size);
    result = mxs_pcre2_substitute(re2, subject, good_replace, &dest, &size);
    test_assert(result == MXS_PCRE2_NOMATCH, "Non-matching substitution should not substitute");

    size = 1000;
    dest = (char*)MXS_REALLOC(dest, size);
    result = mxs_pcre2_substitute(re, subject, bad_replace, &dest, &size);
    test_assert(result == MXS_PCRE2_ERROR, "Bad substitution should return an error");

    MXS_FREE(dest);
    pcre2_code_free(re);
    pcre2_code_free(re2);
    return 0;
}

static int test3()
{
    mxb::Regex r1;
    test_assert(!r1.valid(), "Empty regex is not valid");
    test_assert(r1.empty(), "Empty regex is empty");
    test_assert(!!r1, "Empty regex evaluates to true");
    test_assert(r1.error().empty(), "No errors stored");

    mxb::Regex r2("hello");
    test_assert(r2.valid(), "Regex is valid");
    test_assert(!r2.empty(), "Regex is not empty");
    test_assert(!!r2, "Regex evaluates to true");
    test_assert(r2.error().empty(), "No errors stored");
    test_assert(r2.match("hello"), "Matches exact match");
    test_assert(r2.match("hello world"), "Matches partial match");
    test_assert(r2.pattern() == "hello", "Returned pattern is correct");
    test_assert(r2.replace("hello world", "HELLO") == "HELLO world", "Replaces pattern with string");

    mxb::Regex r3("hello", PCRE2_CASELESS);
    test_assert(r3.error().empty(), "No errors stored");
    test_assert(r3.match("hello world"), "Matches lower case");
    test_assert(r3.match("HELLO WORLD"), "Matches upper case");
    test_assert(r3.match("HeLlO wOrLd"), "Matches mixed case");
    test_assert(r3.replace("hello world", "hi") == "hi world", "Replaces lower case");
    test_assert(r3.replace("HELLO WORLD", "hi") == "hi WORLD", "Replaces upper case");
    test_assert(r3.replace("HeLlO wOrLd", "hi") == "hi wOrLd", "Replaces mixed case");

    mxb::Regex r4("[");
    test_assert(!r4.valid(), "Invalid regex is detected");
    test_assert(!r4, "Invalid regex evaluates to false");
    test_assert(!r4.error().empty(), "Invalid regex has an error message");

    mxb::Regex r5("hello");
    mxb::Regex r6;
    r6 = r5;
    test_assert(r6, "Assigned regex is valid");
    test_assert(!!r6, "Assigned regex evaluates to true");
    test_assert(r6.match("hello world"), "Assigned regex matches");

    mxb::Regex r7(r5);
    test_assert(r7, "Copy-constructed regex is valid");
    test_assert(!!r7, "Copy-constructed regex evaluates to true");
    test_assert(r7.match("hello world"), "Copy-constructed regex matches");

    return 0;
}

int main(int argc, char** argv)
{
    int result = 0;

    result += test1();
    result += test2();
    result += test3();

    return result;
}

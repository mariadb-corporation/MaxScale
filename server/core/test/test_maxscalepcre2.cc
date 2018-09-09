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

/**
 *
 * @verbatim
 * Revision History
 *
 * Date       Who           Description
 * 05-11-2015 Markus Makela Initial implementation
 *
 * @endverbatim
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
#include <maxscale/alloc.h>
#include <maxscale/pcre2.h>

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

int main(int argc, char** argv)
{
    int result = 0;

    result += test1();
    result += test2();

    return result;
}

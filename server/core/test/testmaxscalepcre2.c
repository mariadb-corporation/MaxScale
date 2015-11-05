/*
 * This file is distributed as part of MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2015
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
#include <maxscale_pcre2.h>
#include <skygw_debug.h>

#define test_assert(a, b) if(!(a)){fprintf(stderr, b);return 1;}

/**
 * Test PCRE2 regular expression simple matching function test
 */
static int test1()
{
    int error = 0;
    mxs_pcre2_result_t result = mxs_pcre2_simple_match("brown.*dog", "The quick brown fox jumps over the lazy dog", 0, &error);
    test_assert(result == MXS_PCRE2_MATCH, "Pattern should match");
    error = 0;
    result = mxs_pcre2_simple_match("BROWN.*DOG", "The quick brown fox jumps over the lazy dog", PCRE2_CASELESS, &error);
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
    pcre2_code *re = pcre2_compile((PCRE2_SPTR) pattern, PCRE2_ZERO_TERMINATED,
                                   0, &err, &erroff, NULL);
    pcre2_code *re2 = pcre2_compile((PCRE2_SPTR) pattern2, PCRE2_ZERO_TERMINATED,
                                    0, &err, &erroff, NULL);
    size_t size = 1000;
    char* dest = malloc(size);
    mxs_pcre2_result_t result = mxs_pcre2_substitute(re, subject, good_replace, &dest, &size);

    test_assert(result == MXS_PCRE2_MATCH, "Substitution should substitute");
    test_assert(strcmp(dest, expected) == 0, "Replaced text should match expected text");

    result = mxs_pcre2_substitute(re2, subject, good_replace, &dest, &size);
    test_assert(result == MXS_PCRE2_NOMATCH, "Non-matching substitution should not substitute");

    result = mxs_pcre2_substitute(re, subject, bad_replace, &dest, &size);
    test_assert(result == MXS_PCRE2_ERROR, "Bad substitution should return an error");
    return 0;
}

int main(int argc, char **argv)
{
    int	result = 0;

	result += test1();
	result += test2();

    return result;
}



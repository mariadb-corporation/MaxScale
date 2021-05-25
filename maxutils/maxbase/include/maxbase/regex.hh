/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>

#if defined (PCRE2_CODE_UNIT_WIDTH)
#error PCRE2_CODE_UNIT_WIDTH already defined. Do not define, and include <maxscale/pcre2.h>.
#else
#define PCRE2_CODE_UNIT_WIDTH 8
#endif

#include <pcre2.h>

#include <string>

namespace maxbase
{

// Class for PCRE2 regular expressions
class Regex
{
public:

    /**
     * Constructs a regular expression
     *
     * The default values construct an empty regular expression that is valid but does not evaluate to true.
     * This is used to signify unconfigured regular expressions.
     *
     * @param pattern The pattern to use.
     * @param options PCRE2 options to use.
     */
    Regex(const std::string& pattern = "", int options = 0);

    Regex(const Regex& rhs);
    Regex(Regex&& rhs);
    ~Regex();

    Regex& operator=(const Regex& rhs);
    Regex& operator=(Regex&& rhs);

    /**
     * @return True if the pattern is empty i.e. the string `""`
     */
    bool empty() const;

    /**
     * @return True if pattern was compiled successfully
     */
    bool valid() const;

    /**
     * @return The human-readable form of the pattern.
     */
    const std::string& pattern() const;

    /**
     * @return The error returned by PCRE2 for invalid patterns
     */
    const std::string& error() const;

    /**
     * Check if `str` matches this pattern
     *
     * @param str  String to match
     * @param data The match data to use. If null, uses built-in match data that is not lock-free.
     *
     * @return True if the string matches the pattern
     */
    bool match(const std::string& str) const;

    /**
     * Replace matches in `str` with given replacement this pattern
     *
     * @param str         String to match
     * @param replacement String to replace matches with
     * @param data        The match data to use. If null, uses built-in match data that is not lock-free.
     *
     * @return True if the string matches the pattern
     */
    std::string replace(const std::string& str, const char* replacement) const;

private:
    std::string m_pattern;
    std::string m_error;
    pcre2_code* m_code = nullptr;
};

/**
 * Replace all occurrences of pattern in string
 *
 * @param re      Compiled pattern to use
 * @param subject Subject string
 * @param replace Replacement string
 * @param error   Pointer to std::string where any error messages are stored
 *
 * @return The replaced string or the original string if no replacement was made. Returns an empty string when
 * any PCRE2 error is encountered.
 */
std::string pcre2_substitute(pcre2_code* re, const std::string& subject,
                             const std::string& replace, std::string* error);
}

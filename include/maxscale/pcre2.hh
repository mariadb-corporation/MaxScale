/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/pcre2.h>
#include <maxscale/utils.hh>

#include <mutex>

namespace maxscale
{

/**
 * @class CloserTraits<pcre2_code*> pcre2.hh <maxscale/pcre2.hh>
 *
 * Specialization of @c CloserTraits for @c pcre2_code*.
 */
template<>
struct CloserTraits<pcre2_code*>
{
    static void close_if(pcre2_code* pCode)
    {
        if (pCode)
        {
            pcre2_code_free(pCode);
        }
    }

    static void reset(pcre2_code*& pCode)
    {
        pCode = NULL;
    }
};

/**
 * @class CloserTraits<pcre2_match_data*> pcre2.hh <maxscale/pcre2.hh>
 *
 * Specialization of @c CloserTraits for @c pcre2_match_data*.
 */
template<>
struct CloserTraits<pcre2_match_data*>
{
    static void close_if(pcre2_match_data* pData)
    {
        if (pData)
        {
            pcre2_match_data_free(pData);
        }
    }

    static void reset(pcre2_match_data*& pData)
    {
        pData = NULL;
    }
};

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
     * @return True if the pattern is not empty and it is valid
     */
    explicit operator bool() const;

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
}

namespace std
{

template<>
class default_delete<pcre2_code>
{
public:
    void operator()(pcre2_code* p)
    {
        if (p)
        {
            pcre2_code_free(p);
        }
    }
};
}

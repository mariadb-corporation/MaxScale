/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxbase/ccdefs.hh>
#include <algorithm>
#include <string>
#include <sstream>
#include <cstring>
#include <vector>

/**
 * Thread-safe (but not re-entrant) strerror.
 *
 * @param error  An errno value.
 *
 * @return  The corresponding string.
 */
const char* mxb_strerror(int error);

namespace maxbase
{

/**
 * Left trim a string.
 *
 * @param str String to trim.
 * @return @c str
 *
 * @note If there is leading whitespace, the string is moved so that
 *       the returned pointer is always the same as the one given as
 *       argument.
 */
char* ltrim(char* str);

/**
 * Right trim a string.
 *
 * @param str String to trim.
 * @return @c str
 *
 * @note The returned pointer is always the same the one given as
 *       argument.
 */
char* rtrim(char* str);

/**
 * Left and right trim a string.
 *
 * @param str String to trim.
 * @return @c str
 *
 * @note If there is leading whitespace, the string is moved so that
 *       the returned pointer is always the same the one given as
 *       argument.
 */
char* trim(char* str);

/**
 * @brief Left trim a string.
 *
 * @param s  The string to be trimmed.
 */
inline void ltrim(std::string& s)
{
    s.erase(s.begin(),
            std::find_if(s.begin(),
                         s.end(),
                         std::not1(std::ptr_fun<int, int>(std::isspace))));
}

/**
 * @brief Right trim a string.
 *
 * @param s  The string to be trimmed.
 */
inline void rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(),
                         s.rend(),
                         std::not1(std::ptr_fun<int, int>(std::isspace))).base(),
            s.end());
}

/**
 * @brief Trim a string.
 *
 * @param s  The string to be trimmed.
 */
inline void trim(std::string& s)
{
    ltrim(s);
    rtrim(s);
}

/**
 * @brief Left-trimmed copy of a string.
 *
 * @param s  The string to the trimmed.
 *
 * @return A left-trimmed copy of the string.
 */
inline std::string ltrimmed_copy(const std::string& original)
{
    std::string s(original);
    ltrim(s);
    return s;
}

/**
 * @brief Right-trimmed copy of a string.
 *
 * @param s  The string to the trimmed.
 *
 * @return A right-trimmed copy of the string.
 */
inline std::string rtrimmed_copy(const std::string& original)
{
    std::string s(original);
    rtrim(s);
    return s;
}

/**
 * @brief Trimmed copy of a string.
 *
 * @param s  The string to the trimmed.
 *
 * @return A trimmed copy of the string.
 */
inline std::string trimmed_copy(const std::string& original)
{
    std::string s(original);
    ltrim(s);
    rtrim(s);
    return s;
}

/**
 * Tokenize a string
 *
 * @param str   String to tokenize
 * @param delim List of delimiters (see strtok(3))
 *
 * @return List of tokenized strings
 */
inline std::vector<std::string> strtok(std::string str, const char* delim)
{
    std::vector<std::string> rval;
    char* save_ptr;
    char* tok = strtok_r(&str[0], delim, &save_ptr);

    while (tok)
    {
        rval.emplace_back(tok);
        tok = strtok_r(NULL, delim, &save_ptr);
    }

    return rval;
}

/**
 * Join objects into a string delimited by separators
 *
 * @param container Container that provides iterators, stored value must support writing to ostream with
 *                  operator<<
 * @param separator Value used as the separator
 *
 * @return String created by joining all values and delimiting them with `separator` (no trailing delimiter)
 */
template<class T>
std::string join(const T& container, const std::string& separator = ",")
{
    std::stringstream ss;
    auto it = begin(container);

    if (it != end(container))
    {
        ss << *it++;

        while (it != end(container))
        {
            ss << separator << *it++;
        }
    }

    return ss.str();
}
}

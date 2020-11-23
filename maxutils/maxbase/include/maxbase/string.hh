/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

/**
 * Generate std::string to_string(const T&) for any type T for which there is a
 * function std::ostream& operator<<(std::ostream&, const T&) declared.
 */
template<class T,
         typename std::remove_reference<decltype(operator<<(*(std::ostream*)nullptr, *(T*)(0)))>::type* =
             nullptr>
std::string to_string(const T& t)
{
    std::ostringstream os;
    os << t;
    return os.str();
}

/**
 * macro MAKE_STR - Make a string out of streaming operations:
 *                  db.query(MAKE_STR("SELECT col FROM table WHERE id = " << id));
 */
#define MAKE_STR(sstr) \
    [&]() { \
        std::ostringstream os; \
        os << sstr; \
        return os.str(); \
    } ()

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
 * @brief lower_case
 * @param str
 */
inline void lower_case(std::string& str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
}

/**
 * @brief upper_case
 * @param str
 */
inline void upper_case(std::string& str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
}

/**
 * @brief lower_case_copy
 * @param str
 * @return
 */
inline std::string lower_case_copy(const std::string& str)
{
    std::string ret(str.size(), 0);
    std::transform(str.begin(), str.end(), ret.begin(), ::tolower);
    return ret;
}

/**
 * @brief upper_case_copy
 * @param str
 * @return
 */
inline std::string upper_case_copy(const std::string& str)
{
    std::string ret(str.size(), 0);
    std::transform(str.begin(), str.end(), ret.begin(), ::toupper);
    return ret;
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
 * @param quotation Quotation marker used to quote the values
 *
 * @return String created by joining all values and delimiting them with `separator` (no trailing delimiter)
 */
template<class T>
std::string join(const T& container, const std::string& separator = ",", const std::string& quotation = "")
{
    std::ostringstream ss;
    auto it = std::begin(container);

    if (it != std::end(container))
    {
        ss << quotation << *it++ << quotation;

        while (it != std::end(container))
        {
            ss << separator << quotation << *it++ << quotation;
        }
    }

    return ss.str();
}

/**
 * Convert a string to a long.
 *
 * @param s      The string to convert.
 * @param base   The base; must be as specified for strtol.
 * @param value  On successful return, the corresponding value. Can be nullptr
 *               in which case the function can be used for merely checking that
 *               a string can be converted to a long.
 *
 * @return True, if the string could be converted.
 */
bool get_long(const char* s, int base, long* value);

inline bool get_long(const std::string& s, int base, long* value)
{
    return get_long(s.c_str(), base, value);
}

bool get_uint64(const char* s, uint64_t* value);

/**
 * Convert a string to a long, assuming a base of 10.
 *
 * @param s      The string to convert.
 * @param value  On successful return, the corresponding value. Can be nullptr
 *               in which case the function can be used for merely checking that
 *               a string can be converted to a long.
 *
 * @return True, if the string could be converted.
 */
inline bool get_long(const char* s, long* value)
{
    return get_long(s, 10, value);
}

inline bool get_long(const std::string& s, long* value)
{
    return get_long(s.c_str(), 10, value);
}

/**
 * Convert a string to an int.
 *
 * @param s      The string to convert.
 * @param base   The base; must be as specified for strtol.
 * @param value  On successful return, the corresponding value. Can be nullptr
 *               in which case the function can be used for merely checking that
 *               a string can be converted to a long.
 *
 * @return True, if the string could be converted.
 */
bool get_int(const char* s, int base, int* value);

inline bool get_int(const std::string& s, int base, int* value)
{
    return get_int(s.c_str(), base, value);
}

/**
 * Convert a string to an int, assuming a base of 10.
 *
 * @param s      The string to convert.
 * @param value  On successful return, the corresponding value. Can be nullptr
 *               in which case the function can be used for merely checking that
 *               a string can be converted to a long.
 *
 * @return True, if the string could be converted.
 */
inline bool get_int(const char* s, int* value)
{
    return get_int(s, 10, value);
}

inline bool get_int(const std::string& s, int* value)
{
    return get_int(s.c_str(), 10, value);
}

/**
 * Create a human-readable list from the string array. Inserts delimiters between elements.
 *
 * @param elements List elements, copied as is.
 * @param delim Delimiter between elements
 * @param last_delim Delimiter between last two elements. If left empty, uses the regular delimiter.
 * @param quote Quotes to insert around each element
 * @return List as a single string
 */
std::string create_list_string(const std::vector<std::string>& elements,
                               const std::string& delim = ", ", const std::string& last_delim = "",
                               const std::string& quote = "");

/**
 * Convert a string to lower case.
 *
 * @param str String to convert
 */
std::string tolower(const std::string& str);
std::string tolower(const char* str);

template<typename T>
struct StringToTHelper
{
    static T convert(const std::string& str)
    {
        if (str.empty())
        {
            return T();
        }

        T ret;
        std::istringstream os {str};
        os >> ret;

        return ret;
    }
};

template<>
struct StringToTHelper<std::string>
{
    static std::string convert(const std::string& str)
    {
        return str;
    }
};

template<>
struct StringToTHelper<char>
{
    static char convert(const std::string& str)
    {
        if (str.empty())
        {
            return ' ';
        }
        else
        {
            return str[0];
        }
    }
};

/**
 * Strip escape characters from a character string.
 *
 * @param str String to process
 */
void strip_escape_chars(std::string& str);
}

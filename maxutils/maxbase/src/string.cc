/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/string.hh>
#include <ctype.h>
#include <string.h>
#include <limits>

using std::string;

namespace
{

thread_local char errbuf[512];      // Enough for all errors
}

const char* mxb_strerror(int error)
{
#ifdef HAVE_GLIBC
    return strerror_r(error, errbuf, sizeof(errbuf));
#else
    strerror_r(error, errbuf, sizeof(errbuf));
    return errbuf;
#endif
}

namespace maxbase
{

char* ltrim(char* str)
{
    char* ptr = str;

    while (isspace(*ptr))
    {
        ptr++;
    }

    if (ptr != str)
    {
        memmove(str, ptr, strlen(ptr) + 1);
    }

    return str;
}

char* rtrim(char* str)
{
    char* ptr = strchr(str, '\0') - 1;

    while (ptr > str && isspace(*ptr))
    {
        ptr--;
    }

    if (isspace(*(ptr + 1)))
    {
        *(ptr + 1) = '\0';
    }

    return str;
}

char* trim(char* str)
{
    return ltrim(rtrim(str));
}

bool get_long(const char* s, int base, long* value)
{
    errno = 0;
    char* end;
    long l = strtol(s, &end, base);

    bool rv = (*end == 0 && errno == 0);

    if (rv && value)
    {
        *value = l;
    }

    return rv;
}

bool get_uint64(const char* s, uint64_t* value)
{
    errno = 0;
    char* end = nullptr;
    auto ll = strtoull(s, &end, 10);

    bool rv = (*end == 0 && errno == 0);
    if (rv && value)
    {
        *value = ll;
    }
    return rv;
}

bool get_int(const char* s, int base, int* value)
{
    long l;
    bool rv = get_long(s, base, &l);

    if (rv)
    {
        if (l >= std::numeric_limits<int>::min()
            && l <= std::numeric_limits<int>::max())
        {
            if (value)
            {
                *value = l;
            }
        }
        else
        {
            rv = false;
        }
    }

    return rv;
}

std::string create_list_string(const std::vector<string>& elements,
                               const string& delim, const string& last_delim, const std::string& quote)
{
    auto n_elems = elements.size();
    if (n_elems == 0)
    {
        return "";
    }
    else if (n_elems == 1)
    {
        return quote + elements[0] + quote;
    }

    const string& real_last_delim = last_delim.empty() ? delim : last_delim;

    // Need at least one delimiter. Estimate the size of the resulting string to minimize reallocations.
    // Need not be exact.
    auto item_len = std::max(elements[0].length(), elements[1].length())
        + std::max(delim.length(), real_last_delim.length()) + 2 * quote.length();
    auto total_len = item_len * n_elems;
    string rval;
    rval.reserve(total_len);

    auto add_elem = [&rval, &quote](const string& elem, const string& delim) {
            rval += delim;
            rval += quote;
            rval += elem;
            rval += quote;
        };

    add_elem(elements[0], "");      // first element has no delimiter.
    for (size_t i = 1; i < n_elems - 1; i++)
    {
        add_elem(elements[i], delim);
    }
    add_elem(elements[n_elems - 1], real_last_delim);
    return rval;
}

std::string tolower(const std::string& str)
{
    string rval;
    rval.resize(str.length());
    std::transform(str.begin(), str.end(), rval.begin(), ::tolower);
    return rval;
}

std::string tolower(const char* str)
{
    string rval;
    auto len = strlen(str);
    rval.resize(len);
    std::transform(str, str + len, rval.begin(), ::tolower);
    return rval;
}

void strip_escape_chars(string& val)
{
    if (val.length() > 1)
    {
        size_t pos = 0;
        while (pos < val.length())
        {
            if (val[pos] == '\\')
            {
                /* Advance after erasing a character, so that \\ -> \ */
                val.erase(pos, 1);
            }
            pos++;
        }
    }
}

char* strnchr_esc(char* ptr, char c, int len)
{
    char* p = (char*)ptr;
    char* start = p;
    bool quoted = false, escaped = false;
    char qc = 0;

    while (p < start + len)
    {
        if (escaped)
        {
            escaped = false;
        }
        else if (*p == '\\')
        {
            escaped = true;
        }
        else if ((*p == '\'' || *p == '"') && !quoted)
        {
            quoted = true;
            qc = *p;
        }
        else if (quoted && *p == qc)
        {
            quoted = false;
        }
        else if (*p == c && !escaped && !quoted)
        {
            return p;
        }
        p++;
    }

    return NULL;
}

char* strnchr_esc_mariadb(const char* ptr, char c, int len)
{
    char* p = (char*) ptr;
    char* start = p, * end = start + len;
    bool quoted = false, escaped = false, backtick = false, comment = false;
    char qc = 0;

    while (p < end)
    {
        if (escaped)
        {
            escaped = false;
        }
        else if ((!comment && !quoted && !backtick) || (comment && *p == '*')
                 || (!comment && quoted && *p == qc) || (!comment && backtick && *p == '`'))
        {
            switch (*p)
            {
            case '\\':
                escaped = true;
                break;

            case '\'':
            case '"':
                if (!quoted)
                {
                    quoted = true;
                    qc = *p;
                }
                else if (*p == qc)
                {
                    quoted = false;
                }
                break;

            case '/':
                if (p + 1 < end && *(p + 1) == '*')
                {
                    comment = true;
                    p += 1;
                }
                break;

            case '*':
                if (comment && p + 1 < end && *(p + 1) == '/')
                {
                    comment = false;
                    p += 1;
                }
                break;

            case '`':
                backtick = !backtick;
                break;

            case '#':
                return NULL;

            case '-':
                if (p + 2 < end && *(p + 1) == '-'
                    && isspace(*(p + 2)))
                {
                    return NULL;
                }
                break;

            default:
                break;
            }

            if (*p == c && !escaped && !quoted && !comment && !backtick)
            {
                return p;
            }
        }
        p++;
    }
    return NULL;
}
}

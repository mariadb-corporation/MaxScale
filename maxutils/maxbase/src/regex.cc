/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 *
 */

#include <maxbase/regex.hh>
#include <maxbase/log.h>
#include <maxbase/assert.h>

namespace
{
class MatchData
{
public:
    MatchData()
        : m_md_size(16)
        , m_md(pcre2_match_data_create(m_md_size, nullptr))
    {
    }

    operator pcre2_match_data*()
    {
        return m_md;
    }

    void enlarge()
    {
        pcre2_match_data_free(m_md);
        m_md_size *= 2;
        m_md = pcre2_match_data_create(m_md_size, nullptr);
    }

    ~MatchData()
    {
        pcre2_match_data_free(m_md);
    }

private:

    size_t            m_md_size;
    pcre2_match_data* m_md;
};

thread_local struct
{
    MatchData md;
} this_thread;
}


namespace maxbase
{
Regex::Regex(const std::string& pattern, int options)
    : m_pattern(pattern)
{
    if (!m_pattern.empty())
    {
        int err;
        size_t erroff;
        m_code = pcre2_compile((PCRE2_SPTR) pattern.c_str(), pattern.length(), options, &err, &erroff, NULL);

        if (!m_code)
        {
            PCRE2_UCHAR errorbuf[120];
            pcre2_get_error_message(err, errorbuf, sizeof(errorbuf));
            m_error = (const char*)errorbuf;
        }
        else if (pcre2_jit_compile(m_code, PCRE2_JIT_COMPLETE) < 0)
        {
            MXB_ERROR("PCRE2 JIT compilation of pattern '%s' failed.", pattern.c_str());
        }
    }
}

Regex::Regex(const Regex& rhs)
    : Regex(rhs.pattern())
{
}

Regex::Regex(Regex&& rhs)
    : m_pattern(std::move(rhs.m_pattern))
    , m_error(rhs.m_error)
    , m_code(rhs.m_code)
{
    rhs.m_code = nullptr;
}

Regex& Regex::operator=(const Regex& rhs)
{
    Regex tmp(rhs.pattern());
    std::swap(m_code, tmp.m_code);
    std::swap(m_pattern, tmp.m_pattern);
    std::swap(m_error, tmp.m_error);
    return *this;
}

Regex& Regex::operator=(Regex&& rhs)
{
    m_code = rhs.m_code;
    rhs.m_code = nullptr;
    m_pattern = std::move(rhs.m_pattern);
    m_error = rhs.m_error;
    return *this;
}

Regex::~Regex()
{
    pcre2_code_free(m_code);
}

bool Regex::empty() const
{
    return m_pattern.empty();
}

bool Regex::valid() const
{
    return m_code;
}

const std::string& Regex::pattern() const
{
    return m_pattern;
}

const std::string& Regex::error() const
{
    return m_error;
}

bool Regex::match(const std::string& str) const
{
    int rc;

    while ((rc = pcre2_match(m_code, (PCRE2_SPTR)str.c_str(), str.length(), 0, 0, this_thread.md, NULL)) == 0)
    {
        this_thread.md.enlarge();
    }

    return rc > 0;
}

std::string Regex::replace(const std::string& str, const char* replacement) const
{
    std::string output;
    output.resize(str.length());
    size_t size = output.size();

    while (true)
    {
        int rc = pcre2_substitute(
            m_code, (PCRE2_SPTR) str.c_str(), str.length(),
            0, PCRE2_SUBSTITUTE_GLOBAL, this_thread.md, NULL,
            (PCRE2_SPTR) replacement, PCRE2_ZERO_TERMINATED,
            (PCRE2_UCHAR*) &output[0], &size);

        if (rc == PCRE2_ERROR_NOMEMORY)
        {
            size = output.size() * 2;
            output.resize(size);
        }
        else
        {
            break;
        }
    }

    output.resize(size);

    return output;
}

std::string pcre2_substitute(pcre2_code* re,
                             const std::string& subject,
                             const std::string& replace,
                             std::string* error)
{
    mxb_assert(re);
    std::string rval = subject;
    size_t size_tmp = rval.size();
    int rc;

    while ((rc = pcre2_substitute(re, (PCRE2_SPTR) subject.c_str(), subject.length(),
                                  0, PCRE2_SUBSTITUTE_GLOBAL, NULL, NULL,
                                  (PCRE2_SPTR) replace.c_str(), replace.length(),
                                  (PCRE2_UCHAR*) &rval[0], &size_tmp)) == PCRE2_ERROR_NOMEMORY)
    {
        rval.resize(rval.size() * 2 + 1);
        size_tmp = rval.size();
    }

    if (rc < 0)
    {
        if (error)
        {
            char errbuf[1024];
            pcre2_get_error_message(rc, (PCRE2_UCHAR*)errbuf, sizeof(errbuf));
            *error = errbuf;
        }

        rval.clear();
    }
    else
    {
        rval.resize(size_tmp);
    }

    return rval;
}
}

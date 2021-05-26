/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
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

    pcre2_match_data* match_data()
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
Regex::Regex(const std::string& pattern, uint32_t options)
    : m_pattern(pattern)
    , m_options(options)
{
    if (!m_pattern.empty())
    {
        int err;
        size_t erroff;
        auto code = pcre2_compile((PCRE2_SPTR) pattern.c_str(), pattern.length(),
                                  options, &err, &erroff, NULL);

        if (!code)
        {
            PCRE2_UCHAR errorbuf[120];
            pcre2_get_error_message(err, errorbuf, sizeof(errorbuf));
            m_error = (const char*)errorbuf;
        }
        else
        {
            if (pcre2_jit_compile(code, PCRE2_JIT_COMPLETE) < 0)
            {
                MXB_ERROR("PCRE2 JIT compilation of pattern '%s' failed.", pattern.c_str());
            }

            m_code.reset(code, [](auto p) {
                             pcre2_code_free(p);
                         });
        }
    }
}

Regex::Regex(const std::string& pattern, pcre2_code* code, uint32_t options)
    : m_pattern(pattern)
    , m_options(options)
    , m_code(code, [](auto p) {
                 pcre2_code_free(p);
             })
{
}

bool Regex::empty() const
{
    return m_pattern.empty();
}

Regex::operator bool() const
{
    return valid();
}

bool Regex::valid() const
{
    return m_code.get();
}

const std::string& Regex::pattern() const
{
    return m_pattern;
}

const std::string& Regex::error() const
{
    return m_error;
}

bool Regex::match(const char* str, size_t len) const
{
    int rc;
    mxb_assert(m_code.get());

    while ((rc = pcre2_match(m_code.get(), (PCRE2_SPTR)str, len, 0, m_options, this_thread.md, NULL)) == 0)
    {
        this_thread.md.enlarge();
    }

    return rc > 0;
}

std::vector<std::string> Regex::substr(const char* str, size_t len) const
{
    int rc;
    mxb_assert(m_code.get());

    while ((rc = pcre2_match(m_code.get(), (PCRE2_SPTR)str, len, 0, m_options, this_thread.md, NULL)) == 0)
    {
        this_thread.md.enlarge();
    }

    std::vector<std::string> substrings;

    if (rc > 0)
    {
        uint32_t num = 0;
        pcre2_pattern_info(m_code.get(), PCRE2_INFO_CAPTURECOUNT, &num);

        substrings.resize(std::max(rc, (int)num + 1));

        for (int i = 0; i < rc; i++)
        {
            auto& str = substrings[i];
            size_t sz = 0;
            int rc = pcre2_substring_length_bynumber(this_thread.md, i, &sz);

            if (rc == 0)
            {
                // The copying seems to set the terminating null byte so we need one extra byte of space.
                ++sz;
                str.resize(sz);

                if (pcre2_substring_copy_bynumber(this_thread.md, i, (uint8_t*)&str[0], &sz) == 0)
                {
                    // Remove the extra byte we added.
                    str.resize(sz);
                }
                else
                {
                    mxb_assert(!true);
                    return {};
                }
            }
            else if (rc == PCRE2_ERROR_UNSET)
            {
                // A capture that was defined but not captured
                str.clear();
            }
            else
            {
                mxb_assert(!true);
                return {};
            }
        }
    }

    return substrings;
}

std::string Regex::replace(const char* str, size_t len, const char* replacement) const
{
    std::string output;
    output.resize(len);
    size_t size = output.size();

    while (true)
    {
        int rc = pcre2_substitute(
            m_code.get(), (PCRE2_SPTR) str, len,
            0, m_options | PCRE2_SUBSTITUTE_GLOBAL, this_thread.md, NULL,
            (PCRE2_SPTR) replacement, PCRE2_ZERO_TERMINATED,
            (PCRE2_UCHAR*) &output[0], &size);

        if (rc == PCRE2_ERROR_NOMEMORY)
        {
            size = output.size() * 2;
            output.resize(size);
        }
        else
        {
            if (rc < 0)
            {
                PCRE2_UCHAR errorbuf[120] = "";
                pcre2_get_error_message(rc, errorbuf, sizeof(errorbuf));
                m_error = (const char*)errorbuf;
            }

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

Regex::SubstringIndexes Regex::substring_ind_by_name(const char* name) const
{
    SubstringIndexes rval;
    auto md = this_thread.md.match_data();
    auto name_uchar = reinterpret_cast<PCRE2_SPTR>(name);

    int ss_num = pcre2_substring_number_from_name(m_code.get(), name_uchar);
    if (ss_num >= 0)
    {
        int ovec_ind = 2 * ss_num;      // ovector contains pairs of indexes to subject string.
        auto* ptr = pcre2_get_ovector_pointer(md);
        rval.begin = ptr[ovec_ind];
        rval.end = ptr[ovec_ind + 1];
    }
    return rval;
}

std::string Regex::substring_by_name(const char* subject, const char* name) const
{
    std::string rval;
    auto indexes = substring_ind_by_name(name);
    if (!indexes.empty())
    {
        auto ptr_begin = subject + indexes.begin;
        auto ptr_end = subject + indexes.end;
        rval.assign(ptr_begin, ptr_end);
    }
    return rval;
}

bool Regex::SubstringIndexes::empty() const
{
    return end <= begin;
}
}

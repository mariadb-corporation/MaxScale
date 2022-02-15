/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxbase/regex.hh>
#include <maxscale/utils.hh>

#include <mutex>

/**
 * Print an error message explaining an error code.
 * @param errorcode value returned by pcre2 functions
 */
#define MXS_PCRE2_PRINT_ERROR(errorcode) \
    mxs_pcre2_print_error(errorcode, MXS_MODULE_NAME, __FILE__, __LINE__, __func__)

typedef enum
{
    MXS_PCRE2_MATCH,
    MXS_PCRE2_NOMATCH,
    MXS_PCRE2_ERROR
} mxs_pcre2_result_t;

mxs_pcre2_result_t mxs_pcre2_substitute(
    pcre2_code* re, const char* subject, const char* replace, char** dest, size_t* size);
mxs_pcre2_result_t mxs_pcre2_simple_match(const char* pattern, const char* subject, int options, int* error);
/**
 * Print an error message explaining an error code. Best used through the macro
 * MXS_PCRE2_PRINT_ERROR
 */
void mxs_pcre2_print_error(
    int errorcode, const char* module_name, const char* filename, int line_num, const char* func_name);

/**
 * Check that @c subject is valid. A valid subject matches @c re_match yet does
 * not match @c re_exclude. If an error occurs, an error code is written to
 * @c match_error_out.
 *
 * @param re_match If not NULL, the subject must match this to be valid. If NULL,
 * all inputs are considered valid.
 * @param re_exclude If not NULL, will invalidate a matching subject. Even subjects
 * validated by @c re_match can be invalidated. If NULL, invalidates nothing.
 * @param md PCRE2 match data block
 * @param subject Subject string. Should NOT be an empty string.
 * @param length Length of subject. Can be zero for 0-terminated strings.
 * @param calling_module Which module the function was called from. Can be NULL.
 * Used for log messages.
 *
 * @return True, if subject is considered valid. False if subject is not valid or
 * an error occurred.
 */
bool mxs_pcre2_check_match_exclude(pcre2_code* re_match,
    pcre2_code* re_exclude,
    pcre2_match_data* md,
    const char* subject,
    int length,
    const char* calling_module);

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

    static void reset(pcre2_code*& pCode) { pCode = NULL; }
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

    static void reset(pcre2_match_data*& pData) { pData = NULL; }
};
}  // namespace maxscale

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
}  // namespace std

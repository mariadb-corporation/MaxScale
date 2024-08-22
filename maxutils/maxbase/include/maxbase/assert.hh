/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <maxbase/log.hh>

// TODO: Provide an MXB_DEBUG with the same meaning.
#if defined (SS_DEBUG)

namespace maxbase
{
/**
 * Set the frequency of the maybe_error() function
 *
 * @param num The frequency at which maybe_error() returns true
 */
void set_exception_frequency(uint64_t num);

/**
 * Get a "random" error
 *
 * @return If true, a "random" error was generated and whatever is calling it should behave as
 *         if something failed
 */
bool maybe_error();
}

#define mxb_assert(exp) \
        do {if (exp) {} else { \
                const char* mxs_impl_debug_expr = #exp; /** The MXB_ERROR marco doesn't seem to like
                                                         * stringification
                                                         * */\
                fprintf(stderr, \
                        "debug assert at %s:%d failed: %s\n", \
                        (char*)__FILE__, \
                        __LINE__, \
                        mxs_impl_debug_expr); \
                MXB_ERROR("debug assert at %s:%d failed: %s\n", (char*)__FILE__, __LINE__, \
                          mxs_impl_debug_expr); \
                raise(SIGABRT);}} while (false)

#define mxb_assert_message(exp, fmt, ...) \
        do {if (exp) {} else {     \
                const char* mxs_impl_debug_expr = #exp; \
                char mxs_impl_debug_message[1024]; \
                snprintf(mxs_impl_debug_message, sizeof(mxs_impl_debug_message), fmt, ##__VA_ARGS__); \
                fprintf(stderr, \
                        "debug assert at %s:%d failed: %s (%s)\n", \
                        (char*)__FILE__, \
                        __LINE__, \
                        mxs_impl_debug_message, \
                        mxs_impl_debug_expr); \
                MXB_ERROR("debug assert at %s:%d failed: %s (%s)\n", \
                          (char*)__FILE__, \
                          __LINE__, \
                          mxs_impl_debug_message, \
                          mxs_impl_debug_expr); \
                raise(SIGABRT);}} while (false)

#define MXB_AT_DEBUG(exp) exp

#define MXB_MAYBE_RETURN_FALSE() if (mxb::maybe_error()) return false

#else /* SS_DEBUG */

#define mxb_assert(exp)
#define mxb_assert_message(exp, fmt, ...)

#define MXB_AT_DEBUG(exp)

#define MXB_MAYBE_RETURN_FALSE()
#endif /* SS_DEBUG */

namespace maxbase
{

template<class T>
struct always_false
{
    static constexpr bool value = false;
};

template<class T>
inline constexpr bool always_false_v = always_false<T>::value;

}

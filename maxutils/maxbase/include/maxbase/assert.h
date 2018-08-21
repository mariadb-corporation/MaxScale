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

#include <maxbase/cdefs.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <maxbase/log.h>

MXB_BEGIN_DECLS

// TODO: Provide an MXB_DEBUG with the same meaning.
#if defined(SS_DEBUG)

#define mxb_assert(exp) do { if(!(exp)){\
            const char *debug_expr = #exp;  /** The MXB_ERROR marco doesn't seem to like stringification */ \
            MXB_ERROR("debug assert at %s:%d failed: %s\n", (char*)__FILE__, __LINE__, debug_expr); \
            fprintf(stderr, "debug assert at %s:%d failed: %s\n", (char*)__FILE__, __LINE__, debug_expr); \
            raise(SIGABRT);} } while (false)

#define mxb_assert_message(exp,message) do { if(!(exp)){     \
            const char *debug_expr = #exp; \
            MXB_ERROR("debug assert at %s:%d failed: %s (%s)\n", (char*)__FILE__, __LINE__, message, debug_expr); \
            fprintf(stderr, "debug assert at %s:%d failed: %s (%s)\n", (char*)__FILE__, __LINE__, message, debug_expr); \
            raise(SIGABRT);} } while (false)

#define MXB_AT_DEBUG(exp) exp

#else /* SS_DEBUG */

#define mxb_assert(exp)
#define mxb_assert_message(exp, message)

#define MXB_AT_DEBUG(exp)

#endif /* SS_DEBUG */

MXB_END_DECLS

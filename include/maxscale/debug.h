#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/cdefs.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <maxscale/log_manager.h>

MXS_BEGIN_DECLS

#if defined(SS_DEBUG)
#include <maxscale/log_manager.h>
# define ss_dassert(exp) do { if(!(exp)){\
        const char *debug_expr = #exp;  /** The MXS_ERROR marco doesn't seem to like stringification */ \
        MXS_ERROR("debug assert at %s:%d failed: %s\n", (char*)__FILE__, __LINE__, debug_expr); \
        fprintf(stderr, "debug assert at %s:%d failed: %s\n", (char*)__FILE__, __LINE__, debug_expr); \
        raise(SIGABRT);} } while (false)
#define ss_info_dassert(exp,info) do { if(!(exp)){\
        const char *debug_expr = #exp; \
        MXS_ERROR("debug assert at %s:%d failed: %s (%s)\n", (char*)__FILE__, __LINE__, info, debug_expr); \
        fprintf(stderr, "debug assert at %s:%d failed: %s (%s)\n", (char*)__FILE__, __LINE__, info, debug_expr); \
        raise(SIGABRT);} } while (false)
# define ss_debug(exp) exp
# define ss_dfprintf fprintf
# define ss_dfflush  fflush
# define ss_dfwrite  fwrite
#else /* SS_DEBUG */

# define ss_debug(exp)
# define ss_dfprintf(a, b, ...)
# define ss_dfflush(s)
# define ss_dfwrite(a, b, c, d)
# define ss_dassert(exp)
# define ss_info_dassert(exp, info)

#endif /* SS_DEBUG */

MXS_END_DECLS

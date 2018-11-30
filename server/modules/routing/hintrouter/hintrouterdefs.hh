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

#include <maxscale/ccdefs.hh>

#include <maxscale/dcb.hh>
#include <maxscale/log.h>

#if defined (SS_DEBUG)
#define DEBUG_HINTROUTER
#undef DEBUG_HINTROUTER
#else
#undef DEBUG_HINTROUTER
#endif

#ifdef DEBUG_HINTROUTER
#define HR_DEBUG(msg, ...) MXS_NOTICE(msg, ##__VA_ARGS__)
#define HR_ENTRY()         HR_DEBUG(__func__)
#else
#define HR_DEBUG(msg, ...)
#define HR_ENTRY()
#endif

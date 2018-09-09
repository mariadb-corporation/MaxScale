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
#include <unistd.h>
#include <cstring>

namespace maxbase
{

static inline void default_stacktrace_handler(const char* symbol, const char* command)
{
    write(STDOUT_FILENO, symbol, strlen(symbol));
    write(STDOUT_FILENO, ": ", 2);
    write(STDOUT_FILENO, command, strlen(command));
    write(STDOUT_FILENO, "\n", 1);
}

void dump_stacktrace(void (* handler)(const char* symbol, const char* command) = default_stacktrace_handler);
}

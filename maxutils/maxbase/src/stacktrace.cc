/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/stacktrace.hh>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#ifdef HAVE_GLIBC
#include <execinfo.h>
#include <limits.h>
#include <stdarg.h>

namespace
{

static void get_command_output(char* output, size_t size, const char* format, ...)
{
    va_list valist;
    va_start(valist, format);
    int cmd_len = vsnprintf(NULL, 0, format, valist);
    va_end(valist);

    va_start(valist, format);
    char cmd[cmd_len + 1];
    vsnprintf(cmd, cmd_len + 1, format, valist);
    va_end(valist);

    *output = '\0';
    FILE* file = popen(cmd, "r");

    if (file)
    {
        size_t nread = fread(output, 1, size, file);
        nread = nread < size ? nread : size - 1;
        output[nread--] = '\0';

        // Trim trailing newlines
        while (output + nread > output && output[nread] == '\n')
        {
            output[nread--] = '\0';
        }

        pclose(file);
    }
}

static void extract_file_and_line(const char* symbols, char* cmd, size_t size)
{
    const char* filename_end = strchr(symbols, '(');
    const char* symname_end = strchr(symbols, ')');

    if (filename_end && symname_end)
    {
        // This appears to be a symbol in a library
        char filename[PATH_MAX + 1];
        char symname[512];
        char offset[512];
        snprintf(filename, sizeof(filename), "%.*s", (int)(filename_end - symbols), symbols);

        const char* symname_start = filename_end + 1;

        if (*symname_start != '+')
        {
            // We have a string form symbol name and an offset, we need to
            // extract the symbol address

            const char* addr_offset = symname_start;

            while (addr_offset < symname_end && *addr_offset != '+')
            {
                addr_offset++;
            }

            snprintf(symname, sizeof(symname), "%.*s", (int)(addr_offset - symname_start), symname_start);

            if (addr_offset < symname_end && *addr_offset == '+')
            {
                addr_offset++;
            }

            snprintf(offset, sizeof(offset), "%.*s", (int)(symname_end - addr_offset), addr_offset);

            // Get the hexadecimal address of the symbol
            get_command_output(cmd, size,
                               "nm %s |grep ' %s$'|sed -e 's/ .*//' -e 's/^/0x/'",
                               filename, symname);
            long long symaddr = strtoll(cmd, NULL, 16);
            long long offsetaddr = strtoll(offset, NULL, 16);

            // Calculate the file and line now that we have the raw offset into
            // the library
            get_command_output(cmd, size,
                               "addr2line -e %s 0x%x",
                               filename, symaddr + offsetaddr);
        }
        else
        {
            // Raw offset into library
            symname_start++;
            snprintf(symname, sizeof(symname), "%.*s", (int)(symname_end - symname_start), symname_start);
            get_command_output(cmd, size, "addr2line -e %s %s", filename, symname);
        }
    }
}

}

namespace maxbase
{

void dump_stacktrace(void (*handler)(const char* symbol, const char* command))
{
    void *addrs[128];
    int count = backtrace(addrs, 128);
    char** symbols = backtrace_symbols(addrs, count);

    if (symbols)
    {
        for (int n = 0; n < count; n++)
        {
            char cmd[PATH_MAX + 1024] = "<not found>";
            extract_file_and_line(symbols[n], cmd, sizeof(cmd));
            handler(symbols[n], cmd);
        }
        free(symbols);
    }
}

}

#else

namespace maxbase
{

void dump_stacktrace(void (*handler)(const char*, const char*))
{
    // We can't dump stacktraces on non-GLIBC systems
}

}

#endif

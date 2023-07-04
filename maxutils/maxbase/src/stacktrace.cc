/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/stacktrace.hh>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <functional>
#include <cstdarg>
#include <climits>

#include <sys/prctl.h>
#include <dlfcn.h>

#ifdef HAVE_GLIBC
#include <execinfo.h>
#endif

namespace
{

static char cmd[PATH_MAX + 1024];
static char tmp[PATH_MAX + 1024];

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

static void get_command_output_cb(void (* cb)(const char*), const char* format, ...)
{
    va_list valist;
    va_start(valist, format);
    int cmd_len = vsnprintf(NULL, 0, format, valist);
    va_end(valist);

    va_start(valist, format);
    char cmd[cmd_len + 1];
    vsnprintf(cmd, cmd_len + 1, format, valist);
    va_end(valist);

    if (FILE* file = popen(cmd, "r"))
    {
        char buf[512];

        while (size_t n = fread(buf, 1, sizeof(buf) - 1, file))
        {
            buf[n] = '\0';
            cb(buf);
        }

        pclose(file);
    }
}

static void extract_file_and_line(void* symbol, char* cmd, size_t size)
{
    Dl_info info;

    if (dladdr(symbol, &info))
    {
        intptr_t base = (intptr_t)info.dli_fbase;
        intptr_t relocated = (intptr_t)symbol;
        intptr_t offset = relocated;

        if (base != 0x400000)
        {
            // Non-PIE executables load at the address 0x400000 on 64-bit systems. This means the symbol
            // address can be used as-is since symbols in the files use absolute addresses. For relocatable
            // code, we need to subtract the base address from the symbol value to get the offset into the ELF
            // file.
            offset -= base;
        }

        // addr2line outputs the function name and the file and line information on separate lines
        get_command_output(tmp, sizeof(tmp), "addr2line -f -e %s 0x%x", info.dli_fname, offset);
        char* func_start = tmp;
        char* func_end = strchr(func_start, '\n');
        *func_end = '\0';
        char* file_start = func_end + 1;

        const char prefix[] = "MaxScale/";

        // Remove common source prefix
        if (char* str = strstr(file_start, prefix))
        {
            file_start = str + sizeof(prefix) - 1;
        }

        snprintf(cmd, size, "%s (%s): %s", info.dli_fname, tmp, file_start);
    }
    else
    {
        snprintf(cmd, size, "Unknown symbol: %p", symbol);
    }
}
}

namespace maxbase
{

#ifdef HAVE_GLIBC
void dump_stacktrace(std::function<void(const char*)> handler)
{
    void* addrs[128];
    int count = backtrace(addrs, 128);

    int rc = system("command -v addr2line > /dev/null");
    bool do_extract = WIFEXITED(rc) && WEXITSTATUS(rc) == 0;

    // Skip first five frames, they are inside the stacktrace printing function and signal handlers
    int n = 4;

    if (do_extract)
    {
        for (; n < count; n++)
        {
            extract_file_and_line(addrs[n], cmd, sizeof(cmd));
            handler(cmd);
        }
    }
    else if (char** symbols = backtrace_symbols(addrs, count))
    {
        for (; n < count; n++)
        {
            strcpy(cmd, symbols[n]);
            strcat(cmd, ": <binutils not installed>");
            handler(cmd);
        }

        free(symbols);
    }
}

void emergency_stacktrace()
{
    void* addrs[128];
    int count = backtrace(addrs, 128);
    backtrace_symbols_fd(addrs, count, STDOUT_FILENO);
}

void dump_stacktrace(void (* handler)(const char* line))
{
    dump_stacktrace([&](const char* line) {
        handler(line);
    });
}

#else

void dump_stacktrace(void (* handler)(const char*))
{
    // We can't dump stacktraces on non-GLIBC systems
}

void emergency_stacktrace(void (* handler)(const char*))
{
    // We can't dump stacktraces on non-GLIBC systems
}

#endif

void dump_gdb_stacktrace(void (* handler)(const char*))
{
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);
    get_command_output_cb(
        handler,
        "gdb --pid=%d -batch -nx -iex 'set auto-load off' -iex 'set print thread-events off' -ex 'thr a a bt'",
        getpid());
    prctl(PR_SET_PTRACER, 0);
}

bool have_gdb()
{
    int rc = system("command -v gdb > /dev/null");
    return WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}
}

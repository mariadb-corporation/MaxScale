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

#include <maxbase/stacktrace.hh>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <functional>
#include <cstdarg>
#include <climits>

#include <sys/prctl.h>

#ifdef HAVE_GLIBC
#include <execinfo.h>
#endif

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

    *output    = '\0';
    FILE* file = popen(cmd, "r");

    if (file)
    {
        size_t nread    = fread(output, 1, size, file);
        nread           = nread < size ? nread : size - 1;
        output[nread--] = '\0';

        // Trim trailing newlines
        while (output + nread > output && output[nread] == '\n')
        {
            output[nread--] = '\0';
        }

        pclose(file);
    }
}

static void get_command_output_cb(void (*cb)(const char*), const char* format, ...)
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

static void extract_file_and_line(char* symbols, char* cmd, size_t size)
{
    const char* filename_end = strchr(symbols, '(');
    const char* symname_end  = strchr(symbols, ')');

    if (filename_end && symname_end)
    {
        // This appears to be a symbol in a library
        char filename[PATH_MAX + 1];
        char symname[512];
        char offset[512];
        snprintf(filename, sizeof(filename), "%.*s", (int) (filename_end - symbols), symbols);

        const char* symname_start = filename_end + 1;

        if (*symname_start != '+' && symname_start != symname_end)
        {
            // We have a string form symbol name and an offset, we need to
            // extract the symbol address

            const char* addr_offset = symname_start;

            while (addr_offset < symname_end && *addr_offset != '+')
            {
                addr_offset++;
            }

            snprintf(symname, sizeof(symname), "%.*s", (int) (addr_offset - symname_start), symname_start);

            if (addr_offset < symname_end && *addr_offset == '+')
            {
                addr_offset++;
            }

            snprintf(offset, sizeof(offset), "%.*s", (int) (symname_end - addr_offset), addr_offset);

            // Get the hexadecimal address of the symbol
            get_command_output(
                cmd, size, "nm %s |grep ' %s$'|sed -e 's/ .*//' -e 's/^/0x/'", filename, symname);
            long long symaddr    = strtoll(cmd, NULL, 16);
            long long offsetaddr = strtoll(offset, NULL, 16);

            // Calculate the file and line now that we have the raw offset into
            // the library
            get_command_output(cmd, size, "addr2line -e %s 0x%x", filename, symaddr + offsetaddr);
        }
        else
        {
            if (symname_start == symname_end)
            {
                // Symbol is of the format `./executable [0xdeadbeef]`
                if (!(symname_start = strchr(symname_start, '['))
                    || !(symname_end = strchr(symname_start, ']')))
                {
                    snprintf(cmd, size, "Unexpected symbol format");
                    return;
                }
            }

            // Raw offset into library
            symname_start++;
            snprintf(symname, sizeof(symname), "%.*s", (int) (symname_end - symname_start), symname_start);
            get_command_output(cmd, size, "addr2line -e %s %s", filename, symname);
        }

        const char prefix[] = "MaxScale/";

        // Remove common source prefix
        if (char* str = strstr(cmd, prefix))
        {
            str += sizeof(prefix) - 1;
            memmove(cmd, str, strlen(cmd) - (str - cmd) + 1);
        }

        // Remove the address where the symbol is in memory (i.e. the [0xdeadbeef] that follows the
        // (main+0xa1) part), we're only interested where it is in the library.
        if (char* str = strchr(symbols, '['))
        {
            str--;
            *str = '\0';
        }
    }
}
}  // namespace

namespace maxbase
{

#ifdef HAVE_GLIBC
void dump_stacktrace(std::function<void(const char*, const char*)> handler)
{
    void* addrs[128];
    int count      = backtrace(addrs, 128);
    char** symbols = backtrace_symbols(addrs, count);

    int rc          = system("command -v nm > /dev/null && command -v addr2line > /dev/null");
    bool do_extract = WIFEXITED(rc) && WEXITSTATUS(rc) == 0;

    if (symbols)
    {
        // Skip first five frames, they are inside the stacktrace printing function and signal handlers
        for (int n = 4; n < count; n++)
        {
            char cmd[PATH_MAX + 1024] = "<binutils not installed>";

            if (do_extract)
            {
                extract_file_and_line(symbols[n], cmd, sizeof(cmd));
            }

            handler(symbols[n], cmd);
        }
        free(symbols);
    }
}

void dump_stacktrace(void (*handler)(const char* symbol, const char* command))
{
    dump_stacktrace([&](const char* symbol, const char* command) { handler(symbol, command); });
}

#else

void dump_stacktrace(void (*handler)(const char*, const char*))
{
    // We can't dump stacktraces on non-GLIBC systems
}

#endif

void dump_gdb_stacktrace(void (*handler)(const char*))
{
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);
    get_command_output_cb(handler,
        "gdb --pid=%d -batch -nx -iex 'set auto-load off' -iex 'set print thread-events off' -ex 'thr a a "
        "bt'",
        getpid());
    prctl(PR_SET_PTRACER, 0);
}

bool have_gdb()
{
    int rc = system("command -v gdb > /dev/null");
    return WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}
}  // namespace maxbase

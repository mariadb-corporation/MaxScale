/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
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
#include <sys/wait.h>
#include <dlfcn.h>

#ifdef HAVE_GLIBC
#include <execinfo.h>
#endif

namespace
{

static char tmp[PATH_MAX + 1024];
static char cmd[PATH_MAX + sizeof(tmp) + 1024];

using Rename = std::tuple<const char*, const char*>;

// Some name replacements for common templated types. This makes the stacktraces easier to read as they'll
// correspond with what's actually used in the code.
static std::array simplify_names = {
    Rename{
        "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >",
        "std::string"
    },
    Rename{
        "std::chrono::time_point<std::chrono::_V2::steady_clock, std::chrono::duration<long, std::ratio<1l, 1000000000l> > >",
        "std::chrono::steady_clock::time_point"
    },
};

static void run_addr2line(char* output, size_t size, const char* filename, intptr_t offset)
{
    *output = '\0';
    int fd[2];

    if (pipe(fd) == -1)
    {
        return;
    }

    pid_t pid = fork();

    if (pid == 0)
    {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        close(STDERR_FILENO);

        char hex_offset[20];
        sprintf(hex_offset, "0x%lx", offset);
        execlp("addr2line", "addr2line", "-C", "-f", "-e", filename, hex_offset, nullptr);
        _exit(1);   // exec failed if we get here
    }
    else
    {
        close(fd[1]);

        if (pid > 0)
        {
            close(fd[1]);
            ssize_t nread = read(fd[0], output, size);

            if (nread > 0)
            {
                nread = nread < (ssize_t)size ? nread : size - 1;
                output[nread--] = '\0';

                // Trim trailing newlines
                while (output + nread > output && output[nread] == '\n')
                {
                    output[nread--] = '\0';
                }
            }

            int status;
            waitpid(pid, &status, 0);
        }

        close(fd[0]);
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
        run_addr2line(tmp, sizeof(tmp), info.dli_fname, offset);
        char* func_start = tmp;
        char* func_end = strchr(func_start, '\n');

        if (!func_end)
        {
            return;
        }

        *func_end = '\0';
        char* file_start = func_end + 1;

        // Simplify some names
        for (auto [name, replace] : simplify_names)
        {
            int namelen = strlen(name);
            int replacelen = strlen(replace);

            while (char* ptr = strstr(func_start, name))
            {
                memcpy(ptr, replace, replacelen);
                memmove(ptr + replacelen, ptr + namelen, (func_end + 1) - (ptr + namelen));
            }
        }

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

std::string addr_to_symbol(void* addr)
{
    char symname[PATH_MAX + 1024] = "";
    extract_file_and_line(addr, symname, sizeof(symname));
    return symname;
}
}

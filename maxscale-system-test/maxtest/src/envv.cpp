#include "envv.h"

#include <cstring>
#include <cstdarg>

using std::string;

char* readenv(const char * name, const char *format, ...)
{
    char * env = getenv(name);
    if (!env)
    {
        va_list valist;

        va_start(valist, format);
        int message_len = vsnprintf(NULL, 0, format, valist);
        va_end(valist);

        if (message_len < 0)
        {
            return NULL;
        }

        env = (char*)malloc(message_len + 1);

        va_start(valist, format);
        vsnprintf(env, message_len + 1, format, valist);
        va_end(valist);
        setenv(name, env, 1);
    }
    return env;
}

string envvar_get_set(const char* name, const char* format, ...)
{
    string rval;
    const char* old_value = getenv(name);
    if (old_value)
    {
        rval = old_value;
    }
    else if (format)
    {
        va_list valist;
        va_start(valist, format);
        rval = string_printf(format, valist);
        va_end(valist);
        setenv(name, rval.c_str(), 1);
    }
    return rval;
}

int readenv_int(const char * name, int def)
{
    int x;
    char * env = getenv(name);
    if (env)
    {
        sscanf(env, "%d", &x);
    }
    else
    {
        x = def;
        setenv(name, (std::to_string(x).c_str()), 1);
    }
    return x;
}

bool readenv_bool(const char * name, bool def)
{
    char * env = getenv(name);
    if (env)
    {
        return ((strcasecmp(env, "yes") == 0) ||
                (strcasecmp(env, "y") == 0) ||
                (strcasecmp(env, "true") == 0));
    }
    else
    {
        setenv(name, def ? "true" : "false", 1);
        return def;
    }
}

string string_printf(const char* format, ...)
{
    string rval;
    va_list valist;
    va_start(valist, format);
    rval = string_printf(format, valist);
    va_end(valist);
    return rval;
}

std::string string_printf(const char* format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    int bytes_required = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    string rval;
    if (bytes_required > 0)
    {
        int buflen = bytes_required + 1;
        char buf[buflen];
        vsnprintf(buf, buflen, format, args);
        rval = buf;
    }
    return rval;
}

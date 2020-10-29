#include <maxtest/envv.hh>

#include <cstring>
#include <cstdarg>
#include <maxbase/format.hh>

using std::string;

string readenv(const char * name, const char *format, ...)
{
    string rv;
    char* env = getenv(name);
    if (env)
    {
        rv = env;
    }
    else
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

        rv = env;
        free(env);
    }
    return rv;
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
        rval = mxb::string_vprintf(format, valist);
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

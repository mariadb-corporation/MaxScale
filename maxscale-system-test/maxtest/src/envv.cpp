#include <string.h>
#include <string>
#include "envv.h"

char * readenv(const char * name, const char *format, ...)
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

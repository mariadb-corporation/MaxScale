# Module commands

Introduced in MaxScale 2.1, the module commands are special, module-specific
commands. They allow the modules to expand beyond the capabilities of the module
API. Currently, only MaxAdmin implements an interface to the module commands.

All registered module commands can be shown with `maxadmin list commands` and
they can be executed with `maxadmin call command <module> <name> ARGS...` where
_<module>_ is the name of the module and _<name>_ is the name of the command.
_ARGS_ is a command specific list of arguments.

## Developer reference

The module command API is defined in the _modulecmd.h_ header. It consists of
various functions to register and call module commands. Read the function
documentation in the header for more details.

The following example registers the module command _my_command_ for module
_my_module_.

```
#include <maxscale/modulecmd.h>

bool my_simple_cmd(const MODULECMD_ARG *argv)
{
    printf("%d arguments given\n", argv->argc);
}

int main(int argc, char **argv)
{
    modulecmd_arg_type_t my_args[] =
    {
        {MODULECMD_ARG_BOOLEAN, "This is a boolean parameter"},
        {MODULECMD_ARG_STRING | MODULECMD_ARG_OPTIONAL, "This is an optional string parameter"}
    };

    // Register the command
    modulecmd_register_command("my_module", "my_command", my_simple_cmd, 2, my_args);

    // Find the registered command
    const MODULECMD *cmd = modulecmd_find_command("my_module", "my_command");

    // Parse the arguments for the command
    const void *arglist[] = {"true", "optional string"};
    MODULECMD_ARG *arg = modulecmd_arg_parse(cmd, arglist, 2);

    // Call the module command
    modulecmd_call_command(cmd, arg);

    // Free the parsed arguments
    modulecmd_arg_free(arg);
    return 0;
}
```

The array _my_args_ of type _modulecmd_arg_type_t_ is used to tell what kinds of
arguments the command expects. The first argument is a boolean and the second
argument is an optional string.

Arguments are passed to the parsing function as an array of void pointers. They
are interpreted as the types the command expects.

When the module command is executed, the _argv_ parameter for the
_my_simple_cmd_ contains the parsed arguments received from the caller of the
command.

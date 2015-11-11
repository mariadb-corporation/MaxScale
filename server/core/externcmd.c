#include <externcmd.h>

/**
 * Tokenize a string into arguments suitable for a execvp call.
 * @param args Argument string
 * @param argv Array of char pointers to be filled with tokenized arguments
 * @return 0 on success, -1 on error
 */
int tokenize_arguments(char* argstr, char** argv)
{
    int i = 0;
    bool quoted = false;
    bool read = false;
    bool escaped = false;
    char *ptr,*start;
    char args[strlen(argstr)];
    char qc;

    strcpy(args, argstr);
    start = args;
    ptr = start;

    while(*ptr != '\0' && i < MAXSCALE_EXTCMD_ARG_MAX)
    {
	if(escaped)
	{
	    escaped = false;
	}
	else
	{
	    if(*ptr == '\\')
	    {
		escaped = true;
	    }
	    else if(quoted && !escaped && *ptr == qc) /** End of quoted string */
	    {
		*ptr = '\0';
		argv[i++] = strdup(start);
		read = false;
		quoted = false;
	    }
	    else if (!quoted)
	    {
		if(isspace(*ptr))
		{
		    *ptr = '\0';
		    if(read) /** New token */
		    {
			argv[i++] = strdup(start);
			read = false;
		    }
		}
		else if( *ptr == '\"' || *ptr == '\'')
		{
		    /** New quoted token, strip quotes */
		    quoted = true;
		    qc = *ptr;
		    start = ptr + 1;
		}
		else if(!read)
		{
		    start = ptr;
		    read = true;
		}
	    }
	}
	ptr++;
    }
    if(read)
	argv[i++] = strdup(start);
    argv[i] = NULL;

    return 0;
}

/**
 * Allocate a new external command.
 * The name and parameters are copied into the external command structure so
 * the original memory can be freed if needed.
 * @param command Command to execute with the parameters
 * @return Pointer to new external command struct or NULL if an error occurred
 */
EXTERNCMD* externcmd_allocate(char* argstr)
{
    EXTERNCMD* cmd = NULL;

    if (argstr && (cmd = (EXTERNCMD*) malloc(sizeof(EXTERNCMD))) &&
        (cmd->argv = malloc(sizeof(char*) * MAXSCALE_EXTCMD_ARG_MAX)))
    {
        if (tokenize_arguments(argstr, cmd->argv) == 0)
        {
            if (access(cmd->argv[0], X_OK) != 0)
            {
                if (access(cmd->argv[0], F_OK) != 0)
                {
                    skygw_log_write(LE, "Error: Cannot find file: %s", cmd->argv[0]);
                }
                else
                {
                    skygw_log_write(LE, "Error: Cannot execute file '%s'. Missing "
                                    "execution permissions.", cmd->argv[0]);
                }
                externcmd_free(cmd);
                cmd = NULL;
            }
        }
        else
        {
            externcmd_free(cmd);
            cmd = NULL;
        }
    }
    return cmd;
}

/**
 * Free a previously allocated external command.
 * @param cmd Command to free
 */
void externcmd_free(EXTERNCMD* cmd)
{
    if (cmd)
    {
        for (int i = 0; cmd->argv[i]; i++)
        {
            free(cmd->argv[i]);
        }
        free(cmd->argv);
        free(cmd);
    }
}

/**
 *Execute a command in a separate process.
 *@param cmd Command to execute
 *@return 0 on success, -1 on error.
 */
int externcmd_execute(EXTERNCMD* cmd)
{
    int rval = 0;
    pid_t pid;
    
    pid = fork();

    if(pid < 0)
    {
        char errbuf[STRERROR_BUFLEN];
        skygw_log_write(LOGFILE_ERROR,"Error: Failed to execute command '%s', fork failed: [%d] %s",
                        cmd->argv[0],errno,strerror_r(errno, errbuf, sizeof(errbuf)));
        rval = -1;
    }
    else if(pid == 0)
    {
        /** Child process, execute command */
        execvp(cmd->argv[0],cmd->argv);
	_exit(1);
    }
    else
    {
	cmd->child = pid;
	cmd->n_exec++;
	LOGIF(LD,skygw_log_write(LD,"[monitor_exec_cmd] Forked child process %d : %s.",pid,cmd));
    }

    return rval;
}


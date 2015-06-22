#include <externcmd.h>

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

/**
 * Tokenize a string into arguments suitable for a execvp call.
 * @param args Argument string
 * @param argv Array of char pointers to be filled with tokenized arguments
 * @return 0 on success, -1 on error
 */
int tokenize_arguments(char* args, char** argv)
{
    int i = 0;
    bool quoted = false;
    bool read = false;
    bool escaped = false;
    char *ptr,*start;
    char qc;

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
    EXTERNCMD* cmd;

    if(argstr == NULL)
	return NULL;

    if((cmd = (EXTERNCMD*)malloc(sizeof(EXTERNCMD))) != NULL)
    {
	if(tokenize_arguments(argstr,cmd->parameters) == -1)
	{
	    free(cmd);
	    return NULL;
	}
	if(access(cmd->parameters[0],F_OK) != 0)
	{
	    skygw_log_write(LE,
		     "Error: Cannot find file: %s",
		     cmd->parameters[0]);
	    externcmd_free(cmd);
	    return NULL;
	}

	if(access(cmd->parameters[0],X_OK) != 0)
	{
	    skygw_log_write(LE,
		     "Error: Cannot execute file: %s",
		     cmd->parameters[0]);
	    externcmd_free(cmd);
	    return NULL;
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
    int i;

    for(i = 0;cmd->parameters[i] != NULL;i++)
    {
	free(cmd->parameters[i]);
    }
    free(cmd);
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
        skygw_log_write(LOGFILE_ERROR,"Error: Failed to execute command '%s', fork failed: [%d] %s",
                        cmd->parameters[0],errno,strerror(errno));
        rval = -1;
    }
    else if(pid == 0)
    {
        /** Child process, execute command */
        execvp(cmd->parameters[0],cmd->parameters);
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


#include <mon_exec.h>

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

/**
 *Execute a command in a separate process.
 *@param cmd Command to execute
 *@return 0 on success, -1 on error.
 */
int monitor_exec_cmd(char* cmd)
{
    int rval = 0;
    pid_t pid;
    
    pid = fork();

    if(pid < 0)
    {
        skygw_log_write(LOGFILE_ERROR,"Error: Failed to execute command '%s', fork failed: [%d] %s",
                        cmd,errno,strerror(errno));
        rval = -1;
    }
    else if(pid == 0)
    {
        /** Child process, execute command */
        execl(cmd,cmd,NULL);
    }
    else
    {
     LOGIF(LD,skygw_log_write(LD,"[monitor_exec_cmd] Forked child process %d : %s.",pid,cmd));
    }

    return rval;
    
}

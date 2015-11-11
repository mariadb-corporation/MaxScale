#ifndef _EXTERN_CMD_HG
#define _EXTERN_CMD_HG

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <skygw_utils.h>
#include <log_manager.h>
#define MAXSCALE_EXTCMD_ARG_MAX 256

typedef struct extern_cmd_t{
  char** argv; /*< Argument vector for the command, first being the actual command
                * being executed. */
  int n_exec; /*< Number of times executed */
  pid_t child; /*< PID of the child process */
}EXTERNCMD;

EXTERNCMD* externcmd_allocate(char* argstr);
void externcmd_free(EXTERNCMD* cmd);
int externcmd_execute(EXTERNCMD* cmd);
bool externcmd_can_execute(char* argstr);
#endif

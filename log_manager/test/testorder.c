/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <skygw_utils.h>
#include <log_manager.h>

int main(int argc, char** argv)
{
  int iterations = 0, i, interval = 10;
  int block_size;
  int succp, err = 0;
  char cwd[1024];
  char tmp[2048];
  char *message;
  char** optstr;
  long msg_index = 1;
  struct timespec ts1;
  ts1.tv_sec = 0;				
  
  memset(cwd,0,1024);
  if( argc <4){
    fprintf(stderr,
	    "Log Manager Log Order Test\n"
	    "Writes an ascending number into the error log to determine if log writes are in order.\n"
	    "Usage:\t	testorder <iterations> <frequency of log flushes> <size of message in bytes>\n");
    return 1;
  }
  
  block_size = atoi(argv[3]);
  if(block_size < 1 || block_size > 1024){
    fprintf(stderr,"Message size too small or large, must be at least 1 byte long and must not exceed 1024 bytes.");
	return 1;
  }


  if(getcwd(cwd,sizeof(cwd)) == NULL ||
     (optstr = (char**)malloc(sizeof(char*)*4)) == NULL || 
     (message = (char*)malloc(sizeof(char)*block_size))== NULL){
    fprintf(stderr,"Fatal Error, exiting...");
    return 1;
  }
  
  memset(tmp,0,1024);

  sprintf(tmp,"%s",cwd);
  optstr[0] = strdup("log_manager");
  optstr[1] = strdup("-j");
  optstr[2] = strdup(tmp);
  optstr[3] = NULL;

  iterations = atoi(argv[1]);
  interval = atoi(argv[2]);
  
  succp = skygw_logmanager_init( 3, optstr);
  if(!succp)
      fprintf(stderr,"Error, log manager initialization failed.\n");
  ss_dassert(succp);

  skygw_log_disable(LOGFILE_TRACE);
  skygw_log_disable(LOGFILE_MESSAGE);
  skygw_log_disable(LOGFILE_DEBUG);

  for(i = 0;i<iterations;i++){

    sprintf(message,"message|%ld",msg_index++);
	int msgsize = block_size - strlen(message);
	if(msgsize < 0 || msgsize > 8192){
		fprintf(stderr,"Error: Message too long");
		break;
	}
    memset(message + strlen(message), ' ', msgsize);
    memset(message + block_size - 1,'\0',1);
    if(interval > 0 && i % interval == 0){
      err = skygw_log_write_flush(LOGFILE_ERROR, message);
    }else{
      err = skygw_log_write(LOGFILE_ERROR, message);
    }
    if(err){
      fprintf(stderr,"Error: log_manager returned %d",err);
      break;
    }
    ts1.tv_nsec = 100*1000000;
    nanosleep(&ts1, NULL);
  }

  skygw_log_flush(LOGFILE_ERROR);
  skygw_logmanager_done();
  free(message);
  free(optstr[0]);
  free(optstr[1]);
  free(optstr[2]);
  free(optstr[3]);
  free(optstr);
  return 0;
}

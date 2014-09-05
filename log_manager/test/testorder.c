/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
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
  int succp = 0, err = 0;
  char cwd[1024];
  char tmp[2048];
  char message[1024];
  char** optstr;
  long msg_index = 1;

  
  memset(cwd,0,1024);
  if( argc <3 ||
      getcwd(cwd,sizeof(cwd)) == NULL ||
      (optstr = malloc(sizeof(char*)*4)) == NULL){
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
  ss_dassert(succp);

  skygw_log_disable(LOGFILE_TRACE);
  skygw_log_disable(LOGFILE_MESSAGE);
  skygw_log_disable(LOGFILE_DEBUG);

  for(i = 0;i<iterations;i++){

    memset(message,' ',1024);
    sprintf(message,"message:%ld",msg_index++);
    message[1023] = '\0';

    if(interval > 0 && i % interval == 0){
      err = skygw_log_write_flush(LOGFILE_ERROR, message);
    }else{
      err = skygw_log_write(LOGFILE_ERROR, message);
    }
    if(err){
      fprintf(stderr,"Error: log_manager returned %d",err);
      break;
    }
  }

  skygw_logmanager_done();
  free(optstr[0]);
  free(optstr[1]);
  free(optstr[2]);
  free(optstr[3]);
  free(optstr);
  return 0;
}

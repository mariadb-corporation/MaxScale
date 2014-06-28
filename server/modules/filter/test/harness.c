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

/**
 * @file harness.h Test harness for independent testing of filters
 *
 * A test harness that feeds a GWBUF to a chain of filters and prints the results
 * either into a file or to the standard output. 
 *
 * The contents of the GWBUF are either manually set through the standard input
 * or read from a file. The filter chain can be modified and options for the
 * filters are read either from a configuration file or
 * interactively from the command line.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 25/06/14	Markus Makela		Initial implementation
 *
 * @endverbatim
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <filter.h>
#include <buffer.h>
#include <modules.h>
#include <modutil.h>
#include <skygw_utils.h>
#include <log_manager.h>

struct FILTERCHAIN_T
{
  FILTER* filter;
  FILTER_OBJECT* instance;
  SESSION* session;
  DOWNSTREAM* down;
  struct FILTERCHAIN_T* next;

};

typedef struct FILTERCHAIN_T FILTERCHAIN;

/**
 * A container for all the filters, query buffers and user specified parameters
 */
typedef struct
{
  int infile;
  int outfile;
  FILTERCHAIN* head; /**The filter chain*/
  GWBUF* buffer; /**Buffers that are fed to the filter chain*/
  DOWNSTREAM dummyrtr; /**Dummy downstream router for data extraction*/

}HARNESS_INSTANCE;

static HARNESS_INSTANCE instance;

/**
 *A list of available actions.
 */

typedef enum 
  {
    RUNFILTERS,    
    LOAD_FILTER,
    LOAD_CONFIG,
    SET_INFILE,
    SET_OUTFILE,
    CLEAR,
    HELP,
    QUIT,
    UNDEFINED
  } operation_t;

void clear();
operation_t user_input(char*);
void print_help();
int open_file(char* str,int len);
int read_params(FILTER_PARAMETER**, int);
int routeQuery(void* instance, void* session, GWBUF* queue);

int main(int argc, char** argv){
  int running = 1, tklen = 0,  paramc = 0;
  char buffer[256];
  char* tk;
  char* tmp;
  FILTER_PARAMETER** fparams;
  FILTERCHAIN* new_filter;
  const char* query = "select * from tst";
  
  if(!(tmp  = calloc(256, sizeof(char))) ||
     !(instance.head = malloc(sizeof(FILTERCHAIN))) ||
     !(instance.head->down = malloc(sizeof(DOWNSTREAM))) ||
     !(fparams = malloc(sizeof(FILTER_PARAMETER*))))
    {
      printf("Error: Out of memory\n");
      return 1;
    }

  instance.head->down->instance = NULL;
  instance.head->down->session = NULL;
  instance.head->down->routeQuery = routeQuery;
  paramc = 0;
  skygw_logmanager_init(0,NULL);

  while(running){
    printf("Harness> ");
    fgets(buffer,256,stdin);
    tk = strtok(buffer," \n");
    switch(user_input(tk))
      {
      case RUNFILTERS:

	if(instance.buffer != NULL){
	  gwbuf_free(instance.buffer);
	}
	instance.buffer = gwbuf_alloc(strlen(query) + 5);
	gwbuf_set_type(instance.buffer,GWBUF_TYPE_MYSQL);
	
	memcpy(instance.buffer->sbuf->data,query,strlen(query));
	instance.buffer->sbuf->data[4] = 0x03;	
 instance.head->instance->routeQuery(instance.head->filter, instance.head->session,instance.buffer);

	break;

      case LOAD_FILTER:

	new_filter = NULL;
	tk = strtok(NULL," \n");
	strncpy(tmp,tk,strcspn(tk," \n\0"));

	if(instance.head->instance != NULL &&
	   (new_filter = malloc(sizeof(FILTERCHAIN))) != NULL && 
	   (new_filter->down = malloc(sizeof(DOWNSTREAM))) != NULL){
	  new_filter->next = instance.head;
	  new_filter->down->instance = instance.head->filter;
	  new_filter->down->session = instance.head->session;
	  new_filter->down->routeQuery = (void*)instance.head->instance->routeQuery;
	  instance.head = new_filter;
	}

	if((instance.head->instance = (FILTER_OBJECT*)load_module(tmp, MODULE_FILTER)) == NULL)
	  {
	    printf("Error: Filter loading failed.\n");
	    break;
	  }
	
	paramc = read_params(fparams,paramc);

	instance.head->filter = (FILTER*)instance.head->instance->createInstance(NULL,fparams);
	instance.head->session = instance.head->instance->newSession(instance.head->filter, instance.head->session);

	if(instance.head->next != NULL){
	  instance.head->instance->setDownstream(instance.head->filter,
						 instance.head->session,
						 instance.head->next->down);
	}else{
instance.head->instance->setDownstream(instance.head->filter,
						 instance.head->session,
						 instance.head->down);
	}
	break;

      case LOAD_CONFIG:

	break;

      case SET_INFILE:

	tk = strtok(NULL," ");
	tklen = 0;
	while(tk[tklen] != ' ' && tk[tklen] != '\0'){
	  tklen++;
	}
	if(tklen > 0 && tklen < 256){
	  instance.infile = open_file(tk,tklen);
	}else{
	  instance.infile = -1;
	}
	break;

      case SET_OUTFILE:

	tk = strtok(NULL," ");
	tklen = 0;
	while(tk[tklen] != ' ' && tk[tklen] != '\0'){
	  tklen++;
	}
	if(tklen > 0 && tklen < 256){
	  instance.outfile = open_file(tk,tklen);
	}else{
	  instance.outfile = 1;
	}
	break;

      case CLEAR:
	
	clear();
	break;

      case HELP:

	print_help();	
	break;

      case QUIT:

	clear();
	running = 0;
	break;

      default:
	
	break;
	
      }  
  }
  skygw_logmanager_done();
  skygw_logmanager_exit();
  free(tmp);
  free(fparams);
  return 0;
}
void clear()
{
	while(instance.head){
	  FILTERCHAIN* tmph = instance.head;
	  instance.head = instance.head->next;
	  tmph->instance->freeSession(tmph->filter,tmph->session);
	  free(tmph->filter);
	  free(tmph);
	}
	while(instance.buffer){
	  GWBUF* tbuff = instance.buffer->next;
	  gwbuf_free(instance.buffer);
	  instance.buffer = tbuff;
	  
	}
}
operation_t user_input(char* tk)
{
  int tklen = 0;
  char cmpbuff[256];
  while(tk[tklen] != ' ' && tk[tklen] != '\n' && tklen < 255){
    tklen++;
  }
  if(tklen > 0){
    strncpy(cmpbuff,tk,tklen);
    strcat(cmpbuff,"\0");
    if(strcmp(tk,"run")==0){
      return RUNFILTERS;

    }else if(strcmp(cmpbuff,"add")==0){
      return LOAD_FILTER;

    }else if(strcmp(cmpbuff,"clear")==0){
      return CLEAR;

    }else if(strcmp(cmpbuff,"in")==0){
      return SET_INFILE;

    }else if(strcmp(cmpbuff,"out")==0){
      return SET_OUTFILE;

    }else if(strcmp(cmpbuff,"exit")==0 || strcmp(cmpbuff,"quit")==0 || strcmp(cmpbuff,"q")==0){
      return QUIT;

    }else if(strcmp(cmpbuff,"help")==0){
      return HELP;
    }
  }
  return UNDEFINED;
}

/**
 *Prints a short description and a list of available commands.
 */
void print_help()
{

  printf("\nFilter Test Harness\n"
	 "List of commands:\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n"
	 ,"help","Prints this help message."
	 ,"run","Feeds the contents of the buffer to the filter chain."
	 ,"add <filter name>","Loads a filter and appeds it to the end of the chain."
	 ,"clear","Clears the filter chain."
	 ,"in <file name>","Source file for the SQL statements."
	 ,"out <file name>","Destination file for the SQL statements. Defaults to stdout if no parameters were passed."
	 ,"exit","Exit the program"
	 );

}
int open_file(char* str,int len)
{
  int fd = 0;
  char* tmp;
  if((tmp = malloc(sizeof(char)*len))!=NULL){
    strncpy(tmp,str,len);
    fd = open(tmp,O_CREAT|O_RDWR);
    free(tmp);
  }
  return fd;
}

/**
 * Reads filter parameters from the command line as name-value pairs.
 * 
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * !! MEMORY ALLOCATION IS UNCHECKED, FIX AFTER TESTING !!
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 */
int read_params(FILTER_PARAMETER** params, int paramc)
{
  char buffer[256];
  char* token;
  char* names[64];
  char* values[64];
  int pc = 0, do_read = 1;
  int i;
  for(i = 0;i<paramc;i++){
    free(params[i]);
  }
  memset(names,0,64);
  memset(values,0,64);
  printf("Enter filter parametes as <name>=<value>, enter \"done\" to stop.\n");
  while(do_read){

    memset(buffer,0,256);
    printf(">");
    fgets(buffer,255,stdin);
    if(strcmp("done\n",buffer) == 0){
      do_read = 0;
    }else{
      token = strtok(buffer,"=\n");
      if(token!=NULL){
	names[pc] = strdup(token);
      }
      token = strtok(NULL,"=\n");
      if(token!=NULL){      
	values[pc] = strdup(token);
	pc++;
      }
      
    }
    if(pc >= 64){
      do_read = 0;
    }
  }

  params = realloc(params,sizeof(FILTER_PARAMETER*)*(pc+1));
  for(i = 0;i<pc;i++){
    params[i] = malloc(sizeof(FILTER_PARAMETER));
    params[i]->name = names[i];
    params[i]->value = values[i];
  }
  params[pc] = NULL;
  return pc;
}
int routeQuery(void* ins, void* session, GWBUF* queue)
{
  printf("route returned: %s\n", instance.buffer->sbuf->data);
  return 1;
}

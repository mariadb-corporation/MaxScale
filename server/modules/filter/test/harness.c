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
 * 01/07/14	Markus Makela		Initial implementation
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
  GWBUF** buffer; /**Buffers that are fed to the filter chain*/
  int buffer_count;
  DOWNSTREAM dummyrtr; /**Dummy downstream router for data extraction*/

}HARNESS_INSTANCE;

static HARNESS_INSTANCE instance;

/**
 *A list of available actions.
 */

typedef enum 
  {
    UNDEFINED,
    RUNFILTERS,
    LOAD_FILTER,
    LOAD_CONFIG,
    SET_INFILE,
    SET_OUTFILE,
    CLEAR,
    HELP,
    QUIT
  } operation_t;

void clear();
operation_t user_input(char*);
void print_help();
int open_file(char* str,int len);
FILTER_PARAMETER** read_params(int*);
int routeQuery(void* instance, void* session, GWBUF* queue);
void manual_query();
void file_query();

int main(int argc, char** argv){
  int running = 1, tklen = 0,  paramc = 0;
  char buffer[256];
  char* tk;
  char* tmp;
  FILTER_PARAMETER** fparams = NULL;
  FILTERCHAIN* flt_ptr = NULL;
  
  if(!(tmp  = calloc(256, sizeof(char))) ||
     !(instance.head = malloc(sizeof(FILTERCHAIN))) ||
     !(instance.head->down = malloc(sizeof(DOWNSTREAM))))
    {
      printf("Error: Out of memory\n");
      return 1;
    }
  
  instance.infile = -1;
  instance.outfile = -1;
  instance.buffer_count = 0;
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
	if(instance.head->next == NULL){
	  printf("No filters loaded.\n");
	  break;
	}
	if(instance.infile<0){
	  manual_query();
	}else{
	  file_query();
	}
	break;

      case LOAD_FILTER:

	flt_ptr = NULL;
	tk = strtok(NULL," \n");
	strncpy(tmp,tk,strcspn(tk," \n\0"));

	if((flt_ptr = malloc(sizeof(FILTERCHAIN))) != NULL && 
	   (flt_ptr->down = malloc(sizeof(DOWNSTREAM))) != NULL){
	  flt_ptr->next = instance.head;
	  flt_ptr->down->instance = instance.head->filter;
	  flt_ptr->down->session = instance.head->session;
	  if(instance.head->next){
	    flt_ptr->down->routeQuery = (void*)instance.head->instance->routeQuery;
	  }else{
	    flt_ptr->down->routeQuery = (void*)routeQuery;
	  }
	  instance.head = flt_ptr;
	}

	if((instance.head->instance = (FILTER_OBJECT*)load_module(tmp, MODULE_FILTER)) == NULL)
	  {
	    printf("Error: Filter loading failed.\n");
	    flt_ptr = instance.head->next;
	    free(instance.head);
	    instance.head = flt_ptr;
	    break;
	  }
	
	fparams = read_params(&paramc);

	instance.head->filter = (FILTER*)instance.head->instance->createInstance(NULL,fparams);
	instance.head->session = instance.head->instance->newSession(instance.head->filter, instance.head->session);

	instance.head->instance->setDownstream(instance.head->filter,
					       instance.head->session,
					       instance.head->down);

	break;

      case LOAD_CONFIG:

	break;

      case SET_INFILE:

	tk = strtok(NULL," ");
	tklen = strcspn(tk," \n\0");;
	if(tklen > 0 && tklen < 256){
	  instance.infile = open_file(tk,tklen);
	}else{
	  instance.infile = -1;
	}
	break;

      case SET_OUTFILE:

	tk = strtok(NULL," ");
	tklen = strcspn(tk," \n\0");;
	if(tklen > 0 && tklen < 256){
	  instance.outfile = open_file(tk,tklen);
	}else{
	  instance.outfile = -1;
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
      case UNDEFINED:

	printf("Command not found, enter \"help\" for a list of commands\n");

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
    if(tmph->instance){
      tmph->instance->freeSession(tmph->filter,tmph->session);
    }
    free(tmph->filter);
    free(tmph);
  }
  int i;
  for(i = 0;i<instance.buffer_count;i++){
    gwbuf_free(instance.buffer[i]);	  
  }
  free(instance.buffer);
}
operation_t user_input(char* tk)
{  
  if(tk){

    char cmpbuff[256];
    int tklen = strcspn(tk," \n\0");
  
    if(tklen > 0 && tklen < 256){
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
  }
  return UNDEFINED;
}

/**
 *Prints a short description and a list of available commands.
 */
void print_help()
{

  printf("\nFilter Test Harness\n\n"
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
  int fd = -1;
  char* tmp;

  if((tmp = calloc(len+1,sizeof(char)))!=NULL){
    
    strncpy(tmp,str,len);
    fd = open(tmp,O_CREAT|O_RDWR,S_IRWXU|S_IRGRP|S_IXGRP|S_IXOTH);
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
FILTER_PARAMETER** read_params(int* paramc)
{
  char buffer[256];
  char* token;
  char* names[64];
  char* values[64];
  int pc = 0, do_read = 1, val_len = 0;
  int i;

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
	val_len = strcspn(token," \n\0");
	if((names[pc] = calloc((val_len + 1),sizeof(char))) != NULL){
	    memcpy(names[pc],token,val_len);
	}
      }
      token = strtok(NULL,"=\n");
      if(token!=NULL){
	val_len = strcspn(token," \n\0");
	if((values[pc] = calloc((val_len + 1),sizeof(char))) != NULL){
	    memcpy(values[pc],token,val_len);
	}
	pc++;
      }
      
    }
    if(pc >= 64){
      do_read = 0;
    }
  }
  FILTER_PARAMETER** params;
  if((params = malloc(sizeof(FILTER_PARAMETER*)*(pc+1)))!=NULL){
    for(i = 0;i<pc;i++){
      params[i] = malloc(sizeof(FILTER_PARAMETER));
      if(params[i]){
	params[i]->name = strdup(names[i]);
	params[i]->value = strdup(values[i]);
      }
      free(names[i]);
      free(values[i]);
    }
  }
  params[pc] = NULL;
  *paramc = pc;
  return params;
}
int routeQuery(void* ins, void* session, GWBUF* queue)
{
  printf("route returned: %*s\n", (int)GWBUF_LENGTH(queue) - 5, queue->sbuf->data + 5);
  if(instance.outfile>=0){
    lseek(instance.outfile,0,SEEK_END);
    write(instance.outfile,queue->sbuf->data + 5,(int)GWBUF_LENGTH(queue) - 5);
    write(instance.outfile,"\n",1);
  }
  return 1;
}
void manual_query()
{
  char query[1024];
  int qlen,i;
  GWBUF** tmpbuf;
  for(i = 0;i<instance.buffer_count;i++){
    gwbuf_free(instance.buffer[i]);
  }
 
  printf("Enter query: ");
  fgets(query,1024,stdin);

  qlen = strnlen(query, 1024);
  if((tmpbuf = realloc(instance.buffer,sizeof(GWBUF*)))== NULL){
    printf("Error: cannot allocate enough memory.\n");
    return;
  }
  instance.buffer = tmpbuf;
  instance.buffer_count = 1;

  instance.buffer[0] = gwbuf_alloc(qlen + 5);
  gwbuf_set_type(instance.buffer[0],GWBUF_TYPE_MYSQL);
  memcpy(instance.buffer[0]->sbuf->data + 5,query,strlen(query));

  instance.buffer[0]->sbuf->data[0] |= (qlen>>0&1);
  instance.buffer[0]->sbuf->data[0] |= (qlen>>1&1) << 1;
  instance.buffer[0]->sbuf->data[1] |= (qlen>>2&1);
  instance.buffer[0]->sbuf->data[1] |= (qlen>>3&1) << 1;
  instance.buffer[0]->sbuf->data[2] |= (qlen>>4&1);
  instance.buffer[0]->sbuf->data[2] |= (qlen>>5&1) << 1;
  instance.buffer[0]->sbuf->data[3] = 0x00;
  instance.buffer[0]->sbuf->data[4] = 0x03;

  instance.head->instance->routeQuery(instance.head->filter,
				      instance.head->session,
				      instance.buffer[0]);
}

void file_query()
{
  char** query_list;
  char* buff;
  char rc;
  int i, qc = 0, qbuff_sz = 0, buff_sz = 2048;
  int offset = 0, qlen = 0;  
  for(i = 0;i<instance.buffer_count;i++){
    gwbuf_free(instance.buffer[i]);
  }
  
  if((buff = calloc(buff_sz,sizeof(char))) == NULL){
    printf("Error: cannot allocate enough memory.\n");
  }


  while(read(instance.infile,&rc,1)){

    if(rc != '\n'){
      
      if(offset >= buff_sz){
	char* tmp = malloc(sizeof(char)*2*buff_sz);

	if(tmp){
	  memcpy(tmp,buff,buff_sz);
	  free(buff);
	  buff = tmp;
	  buff_sz *= 2;
	}else{
	  printf("Error: cannot allocate enough memory.\n");
	  free(buff);
	  return;
	}
      }

      buff[offset++] = rc;
     
    }else{


      if(qc >= qbuff_sz){
	char** tmpcl = malloc(sizeof(char*) * (qc * 2 + 1));
	if(!tmpcl){
	  printf("Error: cannot allocate enough memory.\n");
	}
	for(i = 0;i < qbuff_sz;i++){
	  tmpcl[i] = query_list[i];
	}
	free(query_list);
	query_list = tmpcl;
	qbuff_sz = qc * 2 + 1;
      }
      
      query_list[qc] = malloc(sizeof(char)*(offset + 1));
      memcpy(query_list[qc],buff,offset);
      query_list[qc][offset] = '\0';
      offset = 0;
      qc++;

    }

  }

  GWBUF** tmpbff = realloc(instance.buffer,sizeof(GWBUF*)*(qc + 1));
  
  if(tmpbff){

    instance.buffer = tmpbff;
    for(i = 0;i<qc;i++){
    
      instance.buffer[i] = gwbuf_alloc(strnlen(query_list[i],buff_sz));
      gwbuf_set_type(instance.buffer[i],GWBUF_TYPE_MYSQL);
      memcpy(instance.buffer[i]->sbuf->data + 5,query_list[i],strnlen(query_list[i],buff_sz));
      
      qlen = strnlen(query_list[i],buff_sz);
      instance.buffer[i]->sbuf->data[0] |= (qlen>>0&1);
      instance.buffer[i]->sbuf->data[0] |= (qlen>>1&1) << 1;
      instance.buffer[i]->sbuf->data[1] |= (qlen>>2&1);
      instance.buffer[i]->sbuf->data[1] |= (qlen>>3&1) << 1;
      instance.buffer[i]->sbuf->data[2] |= (qlen>>4&1);
      instance.buffer[i]->sbuf->data[2] |= (qlen>>5&1) << 1;
      instance.buffer[i]->sbuf->data[3] = 0x00;
      instance.buffer[i]->sbuf->data[4] = 0x03;
    
    }

  }else{
    printf("Error: cannot allocate enough memory for buffers.\n");
  }

  instance.buffer_count = qc;
  
  for(i = 0;i<qc;i++){
    instance.head->instance->routeQuery(instance.head->filter,
					instance.head->session,
					instance.buffer[i]);    
  }

  for(i = 0;i<qc;i++){
    free(query_list[i]);
  }
  free(query_list);
  free(buff);
}

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
#include <ini.h>

#define MODULE_LIMIT 64

/**
 * A single name-value pair and a link to the next item in the 
 * configuration.
 */
typedef struct CONFIG_ITEM_T
{
  char* name;
  char* value;
  struct CONFIG_ITEM_T* next;
}CONFIG_ITEM;

/**
 *A simplified version of a MaxScale configuration context used to load filters
 * and their options.
 */
typedef struct CONFIG_T
{
  char* section;
  CONFIG_ITEM* item;
  struct CONFIG_T* next;
  
}CONFIG;

/**
 *A structure that holds all the necessary information to emulate a working
 * filter environment.
 */
struct FILTERCHAIN_T
{
  FILTER* filter; /**An instance of a particular filter*/
  FILTER_OBJECT* instance; /**Dynamically loaded module*/
  SESSION* session; /**A session with a single filter*/
  DOWNSTREAM* down; /**The next filter in the chain*/
  char* name; /**Module name*/
  struct FILTERCHAIN_T* next;

};

typedef struct FILTERCHAIN_T FILTERCHAIN;

/**
 * A container for all the filters, query buffers and user specified parameters
 */
typedef struct
{
  int infile; /**A file where the queries are loaded from*/
  int outfile; /**A file where the output of the filters is logged*/
  FILTERCHAIN* head; /**The filter chain*/
  GWBUF** buffer; /**Buffers that are fed to the filter chain*/
  int buffer_count;
  DOWNSTREAM dummyrtr; /**Dummy downstream router for data extraction*/
  CONFIG* conf; /**Configurations loaded from a file*/
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
int open_file(char* str);
FILTERCHAIN* load_filter_module(char* str);
int load_filter(FILTERCHAIN*, CONFIG*);
FILTER_PARAMETER** read_params(int*);
int routeQuery(void* instance, void* session, GWBUF* queue);
void manual_query();
void file_query();
static int handler(void* user, const char* section, const char* name,const char* value);
CONFIG* process_config(CONFIG*);
int load_config(char* fname);

int main(int argc, char** argv){
  int running = 1, tklen = 0;
  char buffer[256];
  char* tk;
  char* tmp;
  FILTER_PARAMETER** fparams = NULL;
  
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
  instance.conf = NULL;
  instance.head->down->routeQuery = routeQuery;
  skygw_logmanager_init(0,NULL);

  while(running){
    printf("Harness> ");
    memset(buffer,0,256);
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

	tk = strtok(NULL," \n");
	instance.head = load_filter_module(tk);
	if(!instance.head || !load_filter(instance.head,instance.conf)){
	  printf("Error creating filter instance.\n");
	  
	}

	break;

      case LOAD_CONFIG:
	tk = strtok(NULL,"  \n\0");
	if(!load_config(tk)){
	  printf("Error loading configuration file.\n");
	}
	break;

      case SET_INFILE:

	tk = strtok(NULL,"  \n\0");
	tklen = strcspn(tk," \n\0");;

	if(tklen > 0 && tklen < 256){

	  instance.infile = open_file(tk);

	}else{

	  instance.infile = -1;

	}

	break;

      case SET_OUTFILE:

	tk = strtok(NULL,"  \n\0");
	tklen = strcspn(tk," \n\0");;

	if(tklen > 0 && tklen < 256){

	  instance.outfile = open_file(tk);

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

	running = 0;
	break;
      case UNDEFINED:

	printf("Command not found, enter \"help\" for a list of commands\n");

	break;
      default:
	
	break;
	
      }  
  }
  if(instance.infile >= 0){
    close(instance.infile);
  }
  if(instance.outfile >= 0){
    close(instance.outfile);
  }
  clear();
  skygw_logmanager_done();
  skygw_logmanager_exit();
  free(instance.head);
  free(tmp);
  free(fparams);
  return 0;
}
/**
 *Clears the filters and all the query buffers.
 */
void clear()
{
  if(instance.head){
      while(instance.head->next){
      FILTERCHAIN* tmph = instance.head;
      instance.head = instance.head->next;
      if(tmph->instance){
	tmph->instance->freeSession(tmph->filter,tmph->session);
      }
      free(tmph->filter);
      free(tmph);
    }
  }
  int i;
  for(i = 0;i<instance.buffer_count;i++){
    gwbuf_free(instance.buffer[i]);	  
  }
  free(instance.buffer);
}

/**
 * Converts the passed string into an operation
 *
 * @param tk The string to parse
 * @return The operation to perform or UNDEFINED, if parsing failed
 */
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

      }else if(strcmp(cmpbuff,"config")==0){
	return LOAD_CONFIG;

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
 *Prints a list of available commands.
 */
void print_help()
{

  printf("\nFilter Test Harness\n\n"
	 "List of commands:\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n"
	 ,"help","Prints this help message."
	 ,"run","Feeds the contents of the buffer to the filter chain."
	 ,"add <filter name>","Loads a filter and appeds it to the end of the chain."
	 ,"clear","Clears the filter chain."
	 ,"config <file name>","Loads filter configurations from a file."
	 ,"in <file name>","Source file for the SQL statements."
	 ,"out <file name>","Destination file for the SQL statements. Defaults to stdout if no parameters were passed."
	 ,"exit","Exit the program"
	 );

}

/**
 *Opens a file for reading and writing with adequate permissions.
 *
 * @param str Path to file
 * @return The assigned file descriptor or -1 in case an error occurred
 */
int open_file(char* str)
{
  return open(str,O_CREAT|O_RDWR,S_IRWXU|S_IRGRP|S_IXGRP|S_IXOTH);
}

/**
 * Reads filter parameters from the command line as name-value pairs.
 *
 *@param paramc The number of parameters read is assigned to this variable
 *@return The newly allocated list of parameters with the last one being NULL
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
  unsigned int qlen,i;
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

  instance.buffer[0]->sbuf->data[0] = (qlen>>0&1)|(qlen>>1&1) << 1;
  instance.buffer[0]->sbuf->data[1] = (qlen>>2&1)|(qlen>>3&1) << 1;
  instance.buffer[0]->sbuf->data[2] = (qlen>>4&1)|(qlen>>5&1) << 1;
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
/**
 * Handler for the INI file parser that builds a linked list
 * of all the sections and their name-value pairs.
 * @param user Current configuration.
 * @param section Name of the section.
 * @param name Name of the item.
 * @param value Value of the item.
 * @return Non-zero on success, zero in case parsing is finished.
 */
static int handler(void* user, const char* section, const char* name,
                   const char* value)
{

  CONFIG* conf = instance.conf;
  if(conf == NULL){/**No sections handled*/

    if((conf = malloc(sizeof(CONFIG))) &&
       (conf->item = malloc(sizeof(CONFIG_ITEM)))){

      conf->section = strdup(section);
      conf->item->name = strdup(name);
      conf->item->value = strdup(value);

    }

  }else{

    CONFIG* iter = conf;

    /**Finds the matching section*/
    while(iter){
      if(strcmp(iter->section,section) == 0){
	CONFIG_ITEM* item = malloc(sizeof(CONFIG_ITEM));
	if(item){
	  item->name = strdup(name);
	  item->value = strdup(value);
	  item->next = iter->item;
	  iter->item = item;
	  break;
	}
      }else{
	iter = iter->next;
      }
    }

    /**Section not found, creating a new one*/
    if(iter == NULL){
      
      CONFIG* nxt = malloc(sizeof(CONFIG));
      if(nxt && (nxt->item = malloc(sizeof(CONFIG_ITEM)))){
	nxt->section = strdup(section);
	nxt->item->name = strdup(name);
	nxt->item->value = strdup(value);
	nxt->next = conf;
	conf = nxt;

      }

    }

  }

  instance.conf = conf;
  return 1;
}

/**
 * Removes all non-filter modules from the configuration
 *
 * @param conf A pointer to a configuration struct
 * @return The stripped version of the configuration
 */
CONFIG* process_config(CONFIG* conf)
{
  CONFIG* tmp;
  CONFIG* tail = conf;
  CONFIG* head = NULL;
  CONFIG_ITEM* item;

  while(tail){
    item = tail->item;

    while(item){

      if(strcmp("type",item->name) == 0 &&
	 strcmp("filter",item->value) == 0){
	tmp = tail->next;
	tail->next = head;
	head = tail;
	tail = tmp;
	break;
      }else{
	item = item->next;
      }
    }

    if(item == NULL){
      tail = tail->next;
    }

  }

  return head;
}

/**
 * Reads a MaxScale configuration (or any INI file using MaxScale notation) file and loads only the filter modules in it.
 * 
 * @param fname Configuration file name
 * @return Non-zero on success, zero in case an error occurred.
 */

int load_config( char* fname)
{

  if(ini_parse(fname,handler,instance.conf) < 0){
    return 0;
  }
  
  printf("Configuration loaded from %s:\n\n",fname);
  if(instance.conf == NULL){
    printf("Nothing valid was read from the file.");
    return 0;
  }

  instance.conf = process_config(instance.conf);
  printf("Modules Loaded:\n");
  CONFIG* iter = instance.conf;
  CONFIG_ITEM* item;
  while(iter){
    item = iter->item;
    while(item){
      
      if(!strcmp("module",item->name)){
	
	instance.head = load_filter_module(item->value);	
	if(!instance.head || !load_filter(instance.head,instance.conf)){
	  printf("Error creating filter instance.\nModule: %s\n",item->value);	  
	}else{	  
	  printf("%s\n",iter->section);  
	}
      }
      item = item->next;
    }
    iter = iter->next;
  }
  while(instance.conf){
    item = instance.conf->item;
    while(item){
      item = instance.conf->item;
      instance.conf->item = instance.conf->item->next;
      free(item->name);
      free(item->value);
      free(item);
      item = instance.conf->item;
    }
    instance.conf = instance.conf->next;
    
  }
  return 1;
}

/**
 * Loads a new instance of a filter and starts a new session.
 * This function assumes that the filter module is already loaded.
 * Passing NULL as the CONFIG parameter causes the parameters to be
 * read from the command line one at a time.
 *
 * @param fc The FILTERCHAIN where the new instance and session are created
 * @param cnf A configuration read from a file 
 */
int load_filter(FILTERCHAIN* fc, CONFIG* cnf)
{
  FILTER_PARAMETER** fparams;
  int paramc = -1;

  if(cnf == NULL){
   
    fparams = read_params(&paramc);

  }else{

    CONFIG* iter = cnf;
    CONFIG_ITEM* item;
    while(iter){
      paramc = -1;
      item = iter->item;
      
      while(item){

	/**Matching configuration found*/
	if(!strcmp(item->name,"module") && !strcmp(item->value,fc->name)){
	  paramc = 0;
	  item = iter->item;
	  
	  while(item){
	    if(strcmp(item->name,"module") && strcmp(item->name,"type")){
	      paramc++;
	    }
	    item = item->next;
	  }
	  item = iter->item;
	  fparams = calloc((paramc + 1),sizeof(FILTER_PARAMETER*));
	  if(fparams){
	    
	    int i = 0;
	    while(item){
	      if(strcmp(item->name,"module") != 0 &&
		 strcmp(item->name,"type") != 0){
		fparams[i] = malloc(sizeof(FILTER_PARAMETER));
		if(fparams[i]){
		  fparams[i]->name = strdup(item->name);
		  fparams[i]->value = strdup(item->value);
		  i++;
		}
	      }
	      item = item->next;
	    }

	  }

	}

	if(paramc > -1){
	  break;
	}else{
	  item = item->next;
	}

      }

      if(paramc > -1){
	break;
      }else{
	iter = iter->next;
      }

    }

  }

  if(fc && fc->instance){
    fc->filter = (FILTER*)fc->instance->createInstance(NULL,fparams);
    fc->session = fc->instance->newSession(fc->filter, fc->session);
    if(!fc->filter || !fc->session ){
      return 0;
    }
    fc->instance->setDownstream(fc->filter, fc->session, fc->down); 
  }
  
  if(cnf){
    int x;
    for(x = 0;x<paramc;x++){
      free(fparams[x]->name);
      free(fparams[x]->value);
    }
    free(fparams);
  }

  return 1;
}

/**
 * Loads the filter module and sets the proper downstreams for it
 *
 * The downstream is set to point to the current head of the filter chain
 *
 * @param str Name of the filter module
 * @return Pointer to the newly initialized FILTER_CHAIN or NULL in
 * case module loading failed
 */
FILTERCHAIN* load_filter_module(char* str)
{
  FILTERCHAIN* flt_ptr = NULL;
  char* tmp = strdup(str);
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
    }

  if((flt_ptr->instance = (FILTER_OBJECT*)load_module(tmp, MODULE_FILTER)) == NULL)
    {
      printf("Error: Module loading failed.\n");
      free(flt_ptr->down);
      free(flt_ptr);
      return NULL;
    }
  flt_ptr->name = strdup(str);
  return flt_ptr;
}

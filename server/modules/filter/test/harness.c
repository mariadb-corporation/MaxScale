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
 * The contents of the GWBUF and the filter parameters are either manually set through
 * the command line or read from a file.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 01/07/14	Markus Makela		Initial implementation
 *
 * @endverbatim
 */

#include <harness.h>

int main(int argc, char** argv){
  int running = 1,i;
  char buffer[256];
  char* tk;
  FILTERCHAIN* tmp_chn;
  FILTERCHAIN* del_chn;
  
  if(!(instance.head = malloc(sizeof(FILTERCHAIN))) ||
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
  printf("\n\n\tFilter Test Harness\n\n");
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
	if(instance.buffer == NULL){
	  if(instance.infile<0){
	    manual_query();
	  }else{
	    load_query();
	  }
	}
	for(i = 0;i<instance.buffer_count;i++){
	  instance.head->instance->routeQuery(instance.head->filter,instance.head->session,instance.buffer[i]);
	}
	
	break;

      case LOAD_FILTER:

	tk = strtok(NULL," \n");
	instance.head = load_filter_module(tk);
	if(!instance.head || !load_filter(instance.head,instance.conf)){
	  printf("Error creating filter instance.\n");
	  
	}

	break;
      case DELETE_FILTER:

	tk = strtok(NULL," \n\0");
	tmp_chn = instance.head;
	del_chn = instance.head;

	if(strcmp(instance.head->name,tk) == 0){

	  instance.head = instance.head->next;

	}else{
	
	  while(del_chn->next){

	    if(strcmp(del_chn->name,tk) == 0){

	      tmp_chn->next = del_chn->next;
	      break;
	      
	    }else{
	      tmp_chn = del_chn;
	      del_chn = del_chn->next;
	      

	    }

	  }
	}

	if(del_chn && del_chn->next){

	  printf("Deleted %s.\n",del_chn->name);

	  if(del_chn->instance){

	    del_chn->instance->freeSession(del_chn->filter,del_chn->session);

	  }

	  free(del_chn->filter);
	  free(del_chn->down);
	  free(del_chn->name);
	  free(del_chn);
	}else{
	  printf("No matching filter found.\n");
	}

	break;

      case LOAD_CONFIG:
	tk = strtok(NULL,"  \n\0");
	if(!load_config(tk)){
	  free_filters();
	}
	break;

      case SET_INFILE:

	tk = strtok(NULL,"  \n\0");
	if(instance.infile >= 0){
	  close(instance.infile);
	}
	if(tk!= NULL){
	  
	  instance.infile = open_file(tk);
	  if(instance.infile >= 0){
	    load_query();
	  }
	}else{
	  instance.infile = -1;
	  printf("Queries are read from: command line\n");
	}

	break;

      case SET_OUTFILE:

	tk = strtok(NULL,"  \n\0");
	if(instance.outfile >= 0){
	  close(instance.outfile);
	}
	if(tk!= NULL){
	  
	  instance.outfile = open_file(tk);
	  if(instance.outfile >= 0){
	    printf("Output is logged to: %s\n",tk);
	  }
	}else{
	  instance.outfile = -1;
	  printf("Output logging disabled.\n");
	}

	break;

      case CLEAR:
	
	free_buffers();
	free_filters();	
	break;

      case HELP:

	print_help();	
	break;

      case STATUS:
	
	print_status();
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

  free_buffers();
  free_filters();
  skygw_logmanager_done();
  skygw_logmanager_exit();
  free(instance.head);

  return 0;
}
/**
 * Frees all the loaded filters
 */

void free_filters()
{
  if(instance.head){
    while(instance.head->next){
      FILTERCHAIN* tmph = instance.head;
      instance.head = instance.head->next;
      if(tmph->instance){
	tmph->instance->freeSession(tmph->filter,tmph->session);
      }
      free(tmph->filter);
      free(tmph->down);
      free(tmph->name);
      free(tmph);
    }
  }
}

/**
 * Frees all the query buffers
 */
void free_buffers()
{
  if(instance.buffer){
    int i;
    for(i = 0;i<instance.buffer_count;i++){
      gwbuf_free(instance.buffer[i]);	  
    }
    free(instance.buffer);
    instance.buffer = NULL;
    instance.buffer_count = 0;
  }
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

      }else if(strcmp(cmpbuff,"delete")==0){
	return DELETE_FILTER;

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
      }else if(strcmp(cmpbuff,"status")==0){
	return STATUS;
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
	 "List of commands:\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n"
	 "%-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n"
	 ,"help","Prints this help message."
	 ,"run","Feeds the contents of the buffer to the filter chain."
	 ,"add <filter name>","Loads a filter and appeds it to the end of the chain."
	 ,"delete <filter name>","Deletes a filter."
	 ,"status","Lists all loaded filters and queries"
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
/**
 * Dummy endpoint for the filter chain
 *
 * Prints and logs the contents of the GWBUF after it has passed through all the filters.
 * The packet is handled as a COM_QUERY packet and the packet header is not printed.
 */
int routeQuery(void* ins, void* session, GWBUF* queue)
{
  printf("route returned: %*s\n", (int)(queue->end - (queue->start + 5)), queue->sbuf->data + 5);
  if(instance.outfile>=0){
    write(instance.outfile,queue->start + 5,(int)(queue->end - (queue->start + 5)));
    write(instance.outfile,"\n",1);
  }
  return 1;
}
void manual_query()
{
  char query[1024];
  unsigned int qlen;
  GWBUF** tmpbuf;
 
  free_buffers();

  printf("Enter query: ");
  fgets(query,1024,stdin);

  qlen = strnlen(query, 1024);
  if((tmpbuf = malloc(sizeof(GWBUF*)))== NULL){
    printf("Error: cannot allocate enough memory.\n");
    skygw_log_write(LOGFILE_ERROR,"Error: cannot allocate enough memory.\n");
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

}

void load_query()
{
  char** query_list;
  char* buff;
  char rc;
  int i, qcount = 0, qbuff_sz = 10, buff_sz = 2048;
  int offset = 0;
  unsigned int qlen = 0;
  
  free_buffers();
  
  if((buff = calloc(buff_sz,sizeof(char))) == NULL || 
     (query_list = calloc(qbuff_sz,sizeof(char*))) == NULL){
    printf("Error: cannot allocate enough memory.\n");
    skygw_log_write(LOGFILE_ERROR,"Error: cannot allocate enough memory.\n");
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
	  skygw_log_write(LOGFILE_ERROR,"Error: cannot allocate enough memory.\n");
	  free(buff);
	  return;
	}
      }

      buff[offset++] = rc;
     
    }else{


      if(qcount >= qbuff_sz){
	char** tmpcl = malloc(sizeof(char*) * (qcount * 2 + 1));
	if(!tmpcl){
	  printf("Error: cannot allocate enough memory.\n");
	  skygw_log_write(LOGFILE_ERROR,"Error: cannot allocate enough memory.\n");
	}
	for(i = 0;i < qbuff_sz;i++){
	  tmpcl[i] = query_list[i];
	}
	free(query_list);
	query_list = tmpcl;
	qbuff_sz = qcount * 2 + 1;
      }
      
      query_list[qcount] = malloc(sizeof(char)*(offset + 1));
      memcpy(query_list[qcount],buff,offset);
      query_list[qcount][offset] = '\0';
      offset = 0;
      qcount++;

    }

  }

  GWBUF** tmpbff = malloc(sizeof(GWBUF*)*(qcount + 1));
  if(tmpbff){
    for(i = 0;i<qcount;i++){
    
      tmpbff[i] = gwbuf_alloc(strnlen(query_list[i],buff_sz) + 6);
      gwbuf_set_type(tmpbff[i],GWBUF_TYPE_MYSQL);
      memcpy(tmpbff[i]->sbuf->data + 5,query_list[i],strnlen(query_list[i],buff_sz));
      
      qlen = strnlen(query_list[i],buff_sz);
      tmpbff[i]->sbuf->data[0] = (qlen>>0&1)|(qlen>>1&1) << 1;
      tmpbff[i]->sbuf->data[1] = (qlen>>2&1)|(qlen>>3&1) << 1;
      tmpbff[i]->sbuf->data[2] = (qlen>>4&1)|(qlen>>5&1) << 1;
      tmpbff[i]->sbuf->data[3] = 0x00;
      tmpbff[i]->sbuf->data[4] = 0x03;

    }
    instance.buffer = tmpbff;
  }else{
    printf("Error: cannot allocate enough memory for buffers.\n");
    skygw_log_write(LOGFILE_ERROR,"Error: cannot allocate enough memory for buffers.\n");    
    free_buffers();
  }
  printf("Loaded %d queries from file.\n",qcount);
  instance.buffer_count = qcount;
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
      conf->item->next = NULL;
      conf->next = NULL;

    }

  }else{

    CONFIG* iter = instance.conf;

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
	nxt->item->next = NULL;
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
  CONFIG* iter;
  CONFIG_ITEM* item;
  int config_ok = 1;
  free_filters();
  if(ini_parse(fname,handler,instance.conf) < 0){
    printf("Error parsing configuration file!\n");
    skygw_log_write(LOGFILE_ERROR,"Error parsing configuration file!\n");
    config_ok = 0;
    goto cleanup;
  }
  
  printf("Configuration loaded from %s\n\n",fname);
  if(instance.conf == NULL){
    printf("Nothing valid was read from the file.\n");
    skygw_log_write(LOGFILE_MESSAGE,"Nothing valid was read from the file.\n");
    config_ok = 0;
    goto cleanup;
  }

  instance.conf = process_config(instance.conf);
  if(instance.conf){
    printf("Modules Loaded:\n");
    iter = instance.conf;
  }else{
    printf("No filters found in the configuration file.\n");
    skygw_log_write(LOGFILE_MESSAGE,"No filters found in the configuration file.\n");
    config_ok = 0;
    goto cleanup;
  }

  while(iter){
    item = iter->item;
    while(item){
      
      if(!strcmp("module",item->name)){
	
	instance.head = load_filter_module(item->value);	
	if(!instance.head || !load_filter(instance.head,instance.conf)){

	  printf("Error creating filter instance!\nModule: %s\n",item->value);
	  skygw_log_write(LOGFILE_ERROR,"Error creating filter instance!\nModule: %s\n",item->value);
	  config_ok = 0;
	  goto cleanup;

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

 cleanup:
  while(instance.conf){
    iter = instance.conf;
    instance.conf = instance.conf->next;
    item = iter->item;

    while(item){      
      free(item->name);
      free(item->value);
      free(item);
      iter->item = iter->item->next;
      item = iter->item;
    }

    free(iter);
  }
  instance.conf = NULL;
  return config_ok;
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

  if((flt_ptr->instance = (FILTER_OBJECT*)load_module(str, MODULE_FILTER)) == NULL)
    {
      printf("Error: Module loading failed: %s\n",str);
      skygw_log_write(LOGFILE_ERROR,"Error: Module loading failed: %s\n",str);
      free(flt_ptr->down);
      free(flt_ptr);
      return NULL;
    }
  flt_ptr->name = strdup(str);
  return flt_ptr;
}
void print_status()
{
  if(instance.head->filter){
    printf("Filters currently loaded:\n\n");  

    FILTERCHAIN* hd = instance.head;
    int i = 1;
    while(hd->filter){
      printf("%d: %s\n", i++, hd->name);
      hd = hd->next;
    }
    
  }else{
    printf("No filters loaded.\n");
  }
  printf("\n");
  if(instance.buffer_count > 0){
    printf("%d queries loaded.\n",instance.buffer_count);
  }else{
    printf("No queries loaded.\n");
  }
}

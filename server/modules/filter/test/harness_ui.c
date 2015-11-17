#include <harness.h>

int main(int argc, char** argv){
  int i;
  char buffer[256];
  char* tk;
  FILTERCHAIN* tmp_chn;
  FILTERCHAIN* del_chn;  
	HARNESS_INSTANCE* hinstance;

	if(harness_init(argc,argv,&hinstance)){
		printf("Error: Initialization failed.\n");
		MXS_ERROR("Initialization failed.\n");
		mxs_log_finish();
		return 1;
	}

  if(instance.verbose){
    printf("\n\n\tFilter Test Harness\n\n");
  }
  
  while(instance.running){
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
	
	route_buffers();
	break;

      case LOAD_FILTER:

	tk = strtok(NULL," \n");
	tmp_chn = load_filter_module(tk);
	if(!tmp_chn || !load_filter(tmp_chn,instance.conf)){
	  printf("Error creating filter instance.\n");	  
	  MXS_ERROR("Error creating filter instance.\n");
	}else{
	  instance.head =  tmp_chn;
	}
	break;

      case DELETE_FILTER:

	tk = strtok(NULL," \n\0");
	tmp_chn = instance.head;
	del_chn = instance.head;
	if(tk){
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
	  free(instance.infile_name);
	}
	if(tk!= NULL){
	  free_buffers();
	  instance.infile = open_file(tk,0);
	  if(instance.infile >= 0){
	    load_query();
	    instance.infile_name = strdup(tk);
	    if(instance.verbose){
	      printf("Loaded %d queries from file '%s'\n",instance.buffer_count,instance.infile_name);
	    }
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
	  free(instance.outfile_name);
	}
	if(tk!= NULL){
	  
	  instance.outfile = open_file(tk,1);
	  if(instance.outfile >= 0){
	    instance.outfile_name = strdup(tk);
	    printf("Output is logged to: %s\n",tk);
	  }
	}else{
	  instance.outfile = -1;
	  printf("Output logging disabled.\n");
	}

	break;

      case SESS_COUNT:

	tk = strtok(NULL,"  \n\0");
	free_buffers();
	free_filters();
	instance.session_count = atoi(tk);
	printf("Sessions set to: %d\n", instance.session_count);
	break;

      case THR_COUNT:

	instance.running = 0;
	pthread_mutex_unlock(&instance.work_mtx);
	for(i = 0;i<instance.thrcount;i++){
	  pthread_join(instance.thrpool[i],NULL);
	}
	pthread_mutex_lock(&instance.work_mtx);

	instance.running = 1;
	tk = strtok(NULL,"  \n\0");
	instance.thrcount = atoi(tk);
	void* t_thr_pool;

	if(!(t_thr_pool = realloc(instance.thrpool,instance.thrcount * sizeof(pthread_t)))){
	  printf("Error: Out of memory\n");
	  MXS_ERROR("Out of memory\n");
	  instance.running = 0;
	  break;
	}

	instance.thrpool = t_thr_pool;
	intptr_t thr_num = 1;

	for(i = 0;i<instance.thrcount;i++){

	  pthread_create(&instance.thrpool[i],
			 NULL,
			 (void*)work_buffer,
			 (void*)thr_num++);

	}
	printf("Threads set to: %d\n", instance.thrcount);

	break;

      case QUIT:

	instance.running = 0;
	pthread_mutex_unlock(&instance.work_mtx);
	for(i = 0;i<instance.thrcount;i++){
	  pthread_join(instance.thrpool[i],NULL);
	}
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
  mxs_log_finish();
  free(instance.head);

  return 0;
}

operation_t user_input(char* tk)
{  

  if(tk){

    char cmpbuff[256];
    int tklen = strcspn(tk," \n\0");
    memset(cmpbuff,0,256);
    if(tklen > 0 && tklen < 256){
      strncpy(cmpbuff,tk,tklen);
      strcat(cmpbuff,"\0");
      if(strcmp(tk,"run")==0 || strcmp(tk,"r")==0){
	return RUNFILTERS;

      }else if(strcmp(cmpbuff,"add")==0){
	return LOAD_FILTER;

      }else if(strcmp(cmpbuff,"delete")==0){
	return DELETE_FILTER;

      }else if(strcmp(cmpbuff,"clear")==0){
	tk = strtok(NULL," \n\0");
	if(tk && !strcmp(tk,"queries")){
	  free_buffers();
	  printf("Queries cleared.\n");
	}else if(tk && !strcmp(tk,"filters")){
	  printf("Filters cleared.\n");
	  free_filters();
	}else{
	  printf("All cleared.\n");
	  free_buffers();
	  free_filters();
	}
	
	
	return OK;

      }else if(strcmp(cmpbuff,"config")==0){
	return LOAD_CONFIG;

      }else if(strcmp(cmpbuff,"in")==0){
	return SET_INFILE;

      }else if(strcmp(cmpbuff,"out")==0){
	return SET_OUTFILE;

      }else if(strcmp(cmpbuff,"exit")==0 || strcmp(cmpbuff,"quit")==0 || strcmp(cmpbuff,"q")==0){
	return QUIT;

      }else if(strcmp(cmpbuff,"help")==0){
	print_help();	
	return OK;
      }else if(strcmp(cmpbuff,"status")==0){
	print_status();
	return OK;
      }else if(strcmp(cmpbuff,"quiet")==0){
	instance.verbose = 0;
	return OK;
      }else if(strcmp(cmpbuff,"verbose")==0){
	instance.verbose = 1;
	return OK;
      }else if(strcmp(cmpbuff,"sessions")==0){
	return SESS_COUNT;
      }else if(strcmp(cmpbuff,"threads")==0){
	return THR_COUNT;
      }
    }
  }
  return UNDEFINED;
}


void print_help()
{

  printf("\nFilter Test Harness\n\n"
	 "List of commands:\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n "
	 "%-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n %-32s%s\n "
	 "%-32s%s\n %-32s%s\n"
	 ,"help","Prints this help message."
	 ,"run","Feeds the contents of the buffer to the filter chain."
	 ,"add <filter name>","Loads a filter and appeds it to the end of the chain."
	 ,"delete <filter name>","Deletes a filter."
	 ,"status","Lists all loaded filters and queries"
	 ,"clear","Clears the filter chain."
	 ,"config <file name>","Loads filter configurations from a file."
	 ,"in <file name>","Source file for the SQL statements."
	 ,"out <file name>","Destination file for the SQL statements. Defaults to stdout if no parameters were passed."
	 ,"threads <number>","Sets the amount of threads to use"
	 ,"sessions <number>","How many sessions to create for each filter. This clears all loaded filters."
	 ,"quiet","Print only error messages."
	 ,"verbose","Print everything."	 
	 ,"exit","Exit the program"
	 );

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
    MXS_ERROR("Cannot allocate enough memory.\n");
    return;
  }
  instance.buffer = tmpbuf;
  instance.buffer_count = 1;

  instance.buffer[0] = gwbuf_alloc(qlen + 5);
  gwbuf_set_type(instance.buffer[0],GWBUF_TYPE_MYSQL);
  memcpy(instance.buffer[0]->sbuf->data + 5,query,qlen);

  instance.buffer[0]->sbuf->data[0] = (qlen);
  instance.buffer[0]->sbuf->data[1] = (qlen << 8);
  instance.buffer[0]->sbuf->data[2] = (qlen << 16);
  instance.buffer[0]->sbuf->data[3] = 0x00;
  instance.buffer[0]->sbuf->data[4] = 0x03;

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

  printf("Using %d threads and %d sessions.\n",instance.thrcount,instance.session_count);

  if(instance.infile_name){
    printf("Input is read from %s.\n",instance.infile_name);
  }
  if(instance.outfile_name){
    printf("Output is written to  %s.\n",instance.outfile_name);
  }
  
}

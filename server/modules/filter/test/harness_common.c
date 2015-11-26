#include <harness.h>


int dcbfun(struct dcb* dcb, GWBUF * buffer)
{
	printf("Data was written to client DCB.\n");
	return 1;
}

int harness_init(int argc, char** argv, HARNESS_INSTANCE** inst){


	int i = 0,rval = 0;  
	MYSQL_session* mysqlsess;
	DCB* dcb;
	char cwd[1024];
	char tmp[2048];

	if(!(argc == 2 && strcmp(argv[1],"-h") == 0)){
		mxs_log_init(NULL,NULL,MXS_LOG_TARGET_DEFAULT);
	}
 
	if(!(instance.head = calloc(1,sizeof(FILTERCHAIN))))
		{
			printf("Error: Out of memory\n");
			MXS_ERROR("Out of memory\n");
      
			return 1;
		}

	*inst = &instance;
	instance.running = 1;
	instance.infile = -1;
	instance.outfile = -1;
	instance.expected = -1;
	instance.buff_ind = -1;
	instance.last_ind = -1;
	instance.sess_ind = -1;
    instance.session = calloc(1,sizeof(SESSION));
	dcb = calloc(1,sizeof(DCB));
	mysqlsess = calloc(1,sizeof(MYSQL_session));

	sprintf(mysqlsess->user,"dummyuser");
	sprintf(mysqlsess->db,"dummydb");		
	dcb->func.write = dcbfun;
	dcb->remote = strdup("0.0.0.0");
	dcb->user = strdup("user");
	instance.session->client = (void*)dcb;
	instance.session->data = (void*)mysqlsess;

	getcwd(cwd,sizeof(cwd));
	sprintf(tmp,"%s",cwd);

	mxs_log_init(NULL, tmp, MXS_LOG_TARGET_DEFAULT);
	
	rval = process_opts(argc,argv);
	
	if(!(instance.thrpool = malloc(instance.thrcount * sizeof(pthread_t)))){
		printf("Error: Out of memory\n");
		MXS_ERROR("Out of memory\n");
		return 1;
	}
  
	/**Initialize worker threads*/
	pthread_mutex_lock(&instance.work_mtx);
	size_t thr_num = 1;
	for(i = 0;i<instance.thrcount;i++){
		rval |= pthread_create(&instance.thrpool[i],NULL,(void*)work_buffer,(void*)thr_num++);
	}

	return rval;
}

void free_filters()
{
	int i;
	if(instance.head){
		while(instance.head->next){
			FILTERCHAIN* tmph = instance.head;

			instance.head = instance.head->next;
			if(tmph->instance){
				for(i = 0;i<instance.session_count;i++){
					if(tmph->filter && tmph->session[i]){
						tmph->instance->freeSession(tmph->filter,tmph->session[i]);
					}
				}
			}
			free(tmph->filter);
			free(tmph->session);
			free(tmph->down);
			free(tmph->name);
			free(tmph);
		}
	}
}

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
  
	if(instance.infile >= 0){
		close(instance.infile);
		free(instance.infile_name);
		instance.infile = -1;
	}
}
int open_file(char* str, unsigned int write)
{
	int mode,fd;

	if(write){
		mode = O_RDWR|O_CREAT;
	}else{
		mode = O_RDONLY;
	}
	if((fd = open(str,mode,S_IRWXU|S_IRGRP|S_IXGRP|S_IXOTH)) < 0){
                char errbuf[STRERROR_BUFLEN];
		printf("Error %d: %s\n", errno, strerror_r(errno, errbuf, sizeof(errbuf)));
	}
	return fd;
}


FILTER_PARAMETER** read_params(int* paramc)
{
	char buffer[256];
	char* token;
        char* saveptr;
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
			token = strtok_r(buffer,"=\n",&saveptr);
			if(token!=NULL){
				val_len = strcspn(token," \n\0");
				if((names[pc] = calloc((val_len + 1),sizeof(char))) != NULL){
					memcpy(names[pc],token,val_len);
				}
			}
			token = strtok_r(NULL,"=\n",&saveptr);
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
	FILTER_PARAMETER** params = NULL;
	if((params = malloc(sizeof(FILTER_PARAMETER*)*(pc+1))) != NULL){
		for(i = 0;i<pc;i++){
			params[i] = malloc(sizeof(FILTER_PARAMETER));
			if(params[i]){
				params[i]->name = strdup(names[i]);
				params[i]->value = strdup(values[i]);
			}
			free(names[i]);
			free(values[i]);
		}	
		params[pc] = NULL;
		*paramc = pc;
	}
	return params;
}

int routeQuery(void* ins, void* session, GWBUF* queue)
{

	unsigned int buffsz = 0;
	char *qstr;

	buffsz = (char*)queue->end - ((char*)queue->start + 5);

	if(queue->hint){
		buffsz += 40;
		if(queue->hint->data){
			buffsz += strnlen(queue->hint->data,1024);
		}
		if(queue->hint->value){
			buffsz += strnlen(queue->hint->value,1024);
		}
	}
	
	qstr = calloc(buffsz + 1,sizeof(char));

	if(qstr){
		memcpy(qstr,queue->start + 5,buffsz);
		if(queue->hint){
			char *ptr = qstr + strlen(qstr);

			switch(queue->hint->type){
			case HINT_ROUTE_TO_MASTER:
				sprintf(ptr,"|HINT_ROUTE_TO_MASTER");
				break;

			case HINT_ROUTE_TO_SLAVE:
				sprintf(ptr,"|HINT_ROUTE_TO_SLAVE");
				break;

			case HINT_ROUTE_TO_NAMED_SERVER:
				sprintf(ptr,"|HINT_ROUTE_TO_NAMED_SERVER");
				break;

			case HINT_ROUTE_TO_UPTODATE_SERVER:
				sprintf(ptr,"|HINT_ROUTE_TO_UPTODATE_SERVER");
				break;

			case HINT_ROUTE_TO_ALL:
				sprintf(ptr,"|HINT_ROUTE_TO_ALL");
				break;
	
			case HINT_PARAMETER:
				sprintf(ptr,"|HINT_PARAMETER");
				break;

			default:
				sprintf(ptr,"|HINT_UNDEFINED");
				break;

			}

			ptr = qstr + strlen(qstr);
			if(queue->hint->data){
				sprintf(ptr,"|%s",(char*)queue->hint->data);
				ptr = qstr + strlen(qstr);
			}
			if(queue->hint->value){
				sprintf(ptr,"|%s",(char*)queue->hint->value);
				ptr = qstr + strlen(qstr);
			}
		}

	}else{
		printf("Error: cannot allocate enough memory.\n");
		MXS_ERROR("cannot allocate enough memory.\n");
		return 0;
	}

	if(instance.verbose){
		printf("Query endpoint: %s\n", qstr);    
	}
  
	if(instance.outfile>=0){
		write(instance.outfile,qstr,strlen(qstr));
		write(instance.outfile,"\n",1);
	}

	free(qstr);
	return 1;
}


int clientReply(void* ins, void* session, GWBUF* queue)
{
  
	if(instance.verbose){
		pthread_mutex_lock(&instance.work_mtx);
		unsigned char* ptr = (unsigned char*)queue->start;
		unsigned int i,pktsize = 4 + ptr[0] + (ptr[1] << 8) + (ptr[2] << 16);
		printf("Reply endpoint: ");
		for(i = 0;i<pktsize;i++){
			printf("%.2x ",*ptr++);
		}
		printf("\n");
		pthread_mutex_unlock(&instance.work_mtx);
	}
  
	if(instance.outfile>=0){
		int qlen = queue->end - queue->start;
		write(instance.outfile,"Reply: ",strlen("Reply: "));
		write(instance.outfile,queue->start,qlen);
		write(instance.outfile,"\n",1);
    
	}

	return 1;
}

/**
 * Read a string from a file descriptor to a block of memory
 * @param fd File descriptor to read from, assumed to be open
 * @param buff Buffer to write to
 * @param size Size of the buffer
 * @return Number of bytes read
 */
int fdgets(int fd, char* buff, int size)
{
	int i = 0;
	
	while(i < size - 1 && read(fd,&buff[i],1))
		{
			if(buff[i] == '\n' || buff[i] == '\0')
				{
					break;
				}
			i++;
		}
	
	buff[i] = '\0';
	return i;
}


/**
 * Loads a query from a file
 *@return 0 if successful, 1 if an error occurred
 */
int load_query()
{
	char** query_list = NULL;
	char* buffer = NULL;
	int i, qcount = 0, qbuff_sz = 10, rval = 0;
	int offset = 0;
	unsigned int qlen = 0;
	buffer = (char*)calloc(4092,sizeof(char));
	if(buffer == NULL){
		printf("Error: cannot allocate enough memory.\n");
		MXS_ERROR("cannot allocate enough memory.\n");
		return 1;
	}

	query_list = calloc(qbuff_sz,sizeof(char*));
	if(query_list == NULL){
		printf("Error: cannot allocate enough memory.\n");
		MXS_ERROR("cannot allocate enough memory.\n");
		free(buffer);
		return 1;
	}


	while((offset = fdgets(instance.infile,buffer,4092))){

		if(qbuff_sz <= qcount){
			char** tmpbuff = realloc(query_list,sizeof(char*)*qbuff_sz*2);
			if(tmpbuff == NULL){
				printf("Error: cannot allocate enough memory.\n");
				MXS_ERROR("cannot allocate enough memory.\n");
				rval = 1;
				goto retblock;
			}
			
			query_list = tmpbuff;
			qbuff_sz *= 2;
			
		}
		
		query_list[qcount] = calloc((offset + 1),sizeof(char));
		strcpy(query_list[qcount],buffer);
		offset = 0;
		qcount++;
		
	}

	/**TODO check what messes up the first querystring*/
	GWBUF** tmpbff = malloc(sizeof(GWBUF*)*(qcount + 1));
	if(tmpbff){
		for(i = 0;i<qcount;i++){
    
			tmpbff[i] = gwbuf_alloc(strlen(query_list[i]) + 6);

			if(tmpbff[i] == NULL)
				{
					printf("Error: cannot allocate a new buffer.\n");
					MXS_ERROR("cannot allocate a new buffer.\n");
					int x;
					for(x = 0;x<i;x++)
						{
							gwbuf_free(tmpbff[x]);
						}
					free(tmpbff);
					rval = 1;
					goto retblock;
				}

			gwbuf_set_type(tmpbff[i],GWBUF_TYPE_MYSQL);
			strcpy((char*)(tmpbff[i]->start + 5),query_list[i]);
			qlen = strlen(query_list[i]) + 1;
			tmpbff[i]->sbuf->data[0] = qlen;
			tmpbff[i]->sbuf->data[1] = (qlen << 8);
			tmpbff[i]->sbuf->data[2] = (qlen << 16);
			tmpbff[i]->sbuf->data[3] = 0x00;
			tmpbff[i]->sbuf->data[4] = 0x03;

		}
		tmpbff[qcount] = NULL;
		instance.buffer = tmpbff;
	}else{
		printf("Error: cannot allocate enough memory for buffers.\n");
		MXS_ERROR("cannot allocate enough memory for buffers.\n");    
		free_buffers();
	    rval = 1;
		goto retblock;
	}

	if(qcount < 1){
		rval = 1;
		goto retblock;
	}
  
	instance.buffer_count = qcount;

	retblock:

	for(i = 0;i<qcount;i++)
		{
			free(query_list[i]);
		}
	free(query_list);
	free(buffer);
	return rval;
}


int handler(void* user, const char* section, const char* name,
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



int load_config( char* fname)
{
	CONFIG* iter;
	CONFIG_ITEM* item;
	int config_ok = 1,inirval;
	free_filters();
	if((inirval = ini_parse(fname,handler,instance.conf)) < 0){
		printf("Error parsing configuration file!\n");
        if(inirval == -1)
            printf("Inih file open error.\n");
        else if(inirval == -2)
            printf("inih memory error.\n");
        MXS_ERROR("Error parsing configuration file!\n");
        config_ok = 0;
        goto cleanup;
	}
	if(instance.verbose){
		printf("Configuration loaded from %s\n\n",fname);
	}
	if(instance.conf == NULL){
		printf("Nothing valid was read from the file.\n");
		MXS_NOTICE("Nothing valid was read from the file.\n");
		config_ok = 0;
		goto cleanup;
	}

	instance.conf = process_config(instance.conf);
	if(instance.conf){
		if(instance.verbose){
			printf("Modules Loaded:\n");
		}
		iter = instance.conf;
	}else{
		printf("No filters found in the configuration file.\n");
		MXS_NOTICE("No filters found in the configuration file.\n");
		config_ok = 0;
		goto cleanup;
	}

	while(iter){
		item = iter->item;
		while(item){
      
			if(!strcmp("module",item->name)){

				if(instance.mod_dir){
					char* modstr = malloc(sizeof(char)*(strlen(instance.mod_dir) + strlen(item->value) + 1));
					strcpy(modstr,instance.mod_dir);
					strcat(modstr,"/");
					strcat(modstr,item->value);
					instance.head = load_filter_module(modstr);
					free(modstr);
				}else{
					instance.head = load_filter_module(item->value);
				}


				if(!instance.head || !load_filter(instance.head,instance.conf)){

					printf("Error creating filter instance!\nModule: %s\n",item->value);
					MXS_ERROR("Error creating filter instance!\nModule: %s\n",item->value);
					config_ok = 0;
					goto cleanup;

				}else{
					if(instance.verbose){
						printf("\t%s\n",iter->section);  
					}
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

int load_filter(FILTERCHAIN* fc, CONFIG* cnf)
{
	FILTER_PARAMETER** fparams = NULL;
	int i, paramc = -1;
	int sess_err = 0;
	int x;
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


	if(cnf && fc && fc->instance){


		fc->filter = (FILTER*)fc->instance->createInstance(NULL,fparams);
		if(fc->filter == NULL){
			printf("Error loading filter:%s: createInstance returned NULL.\n",fc->name);
			sess_err = 1;
			goto error;
		}
		for(i = 0;i<instance.session_count;i++){

			if((fc->session[i] = fc->instance->newSession(fc->filter, instance.session)) &&
			   (fc->down[i] = calloc(1,sizeof(DOWNSTREAM))) &&
			   (fc->up[i] = calloc(1,sizeof(UPSTREAM)))){

				fc->up[i]->session = NULL;
				fc->up[i]->instance = NULL;
				fc->up[i]->clientReply = (void*)clientReply;

				if(fc->instance->setUpstream && fc->instance->clientReply){
					fc->instance->setUpstream(fc->filter, fc->session[i], fc->up[i]);
				}else{
                                    MXS_WARNING("The filter %s does not support client replies.\n",fc->name);
				}

				if(fc->next && fc->next->next){ 

					fc->down[i]->routeQuery = (void*)fc->next->instance->routeQuery;
					fc->down[i]->session = fc->next->session[i];
					fc->down[i]->instance = fc->next->filter;
					fc->instance->setDownstream(fc->filter, fc->session[i], fc->down[i]);

					fc->next->up[i]->clientReply = (void*)fc->instance->clientReply;
					fc->next->up[i]->session = fc->session[i];
					fc->next->up[i]->instance = fc->filter;

					if(fc->instance->setUpstream && fc->instance->clientReply){
						fc->next->instance->setUpstream(fc->next->filter,fc->next->session[i],fc->next->up[i]);
					}

				}else{ /**The dummy router is the next one*/

					fc->down[i]->routeQuery = (void*)routeQuery;
					fc->down[i]->session = NULL;
					fc->down[i]->instance = NULL;
					fc->instance->setDownstream(fc->filter, fc->session[i], fc->down[i]);

				}

			}


			if(!fc->session[i] || !fc->down[i] || !fc->up[i]){

				sess_err = 1;
				break;

			}

		}
    
		if(sess_err){
			for(i = 0;i<instance.session_count;i++){
				if(fc->filter && fc->session[i]){
					fc->instance->freeSession(fc->filter, fc->session[i]);
				}
				free(fc->down[i]);
			}
			free(fc->session);
			free(fc->down);
			free(fc->name);
			free(fc);
		}
    
	}
	error:  


	if(fparams){
		for(x = 0;x<paramc;x++){
			free(fparams[x]->name);
			free(fparams[x]->value);
		}
	}

	free(fparams);

	return sess_err ? 0 : 1;
}


FILTERCHAIN* load_filter_module(char* str)
{
	FILTERCHAIN* flt_ptr = NULL;
	if((flt_ptr = calloc(1,sizeof(FILTERCHAIN))) != NULL && 
	   (flt_ptr->session = calloc(instance.session_count,sizeof(SESSION*))) != NULL &&
	   (flt_ptr->down = calloc(instance.session_count,sizeof(DOWNSTREAM*))) != NULL && 
	   (flt_ptr->up = calloc(instance.session_count,sizeof(UPSTREAM*))) != NULL){
		flt_ptr->next = instance.head;
	}

	if(flt_ptr){
		if( (flt_ptr->instance = (FILTER_OBJECT*)load_module(str, MODULE_FILTER)) == NULL)
			{
				printf("Error: Module loading failed: %s\n",str);
				MXS_ERROR("Module loading failed: %s\n",str);
				free(flt_ptr->down);
				free(flt_ptr);
				return NULL;
			}
		flt_ptr->name = strdup(str);
	}

	return flt_ptr;
}


void route_buffers()
{
	if(instance.buffer_count > 0){
		float tprg = 0.f, bprg = 0.f, trig = 0.f,
			fin = instance.buffer_count*instance.session_count,
			step = (fin/50.f)/fin;
		FILTERCHAIN* fc = instance.head;
    
		while(fc->next->next){
			fc = fc->next;
		}
		instance.tail = fc;

		instance.buff_ind = 0;
		instance.sess_ind = 0;
		instance.last_ind = 0;

		printf("Routing queries...\n");

		if(!instance.verbose){
			printf("%s","|0%");
			float f;
			for(f = 0.f;f<1.f - step*7;f += step){
				printf(" ");
			}
			printf("%s\n","100%|");
			write(1,"|",1);
		}

		while(instance.buff_ind < instance.buffer_count){
			pthread_mutex_unlock(&instance.work_mtx);
			while(instance.last_ind < instance.session_count){
				struct timespec ts1;
				ts1.tv_sec = 0;
	
				tprg = ((bprg + (float)instance.last_ind)/fin);
				if(!instance.verbose){
					if(tprg >= trig){
						write(1,"-",1);
						trig += step;
					}
				}
				ts1.tv_nsec = 100*1000000;
				nanosleep(&ts1, NULL);
			}
			pthread_mutex_lock(&instance.work_mtx);
			instance.buff_ind++;
			bprg += instance.last_ind;
			instance.sess_ind = 0;
			instance.last_ind = 0;

      

		}
		if(!instance.verbose){
			write(1,"|\n",2);
		}
		printf("Queries routed.\n");
	}

}


void work_buffer(void* thr_num)
{
	unsigned int index = instance.session_count;
	GWBUF* fake_ok = gen_packet(PACKET_OK);
	while(instance.running){

		pthread_mutex_lock(&instance.work_mtx);
		pthread_mutex_unlock(&instance.work_mtx);

		index = atomic_add(&instance.sess_ind,1);

		if(instance.running &&
		   index < instance.session_count &&
		   instance.buff_ind < instance.buffer_count)
			{
				struct timespec ts1;
				ts1.tv_sec = 0;				

				if(instance.head->instance->routeQuery(instance.head->filter,

													instance.head->session[index],
													   instance.buffer[instance.buff_ind]) == 0){
					if(instance.outfile > 0){
						const char* msg = "Query returned 0.\n";
						write(instance.outfile,msg,strlen(msg));
					}
				}
				if(instance.tail->instance->clientReply){
					instance.tail->instance->clientReply(instance.tail->filter,
														 instance.tail->session[index],
														 fake_ok);
				}
				atomic_add(&instance.last_ind,1);
				ts1.tv_nsec = 1000*instance.rt_delay*1000000;
				nanosleep(&ts1, NULL);
			}

	}
	gwbuf_free(fake_ok);
}


GWBUF* gen_packet(PACKET pkt)
{
	unsigned int psize = 0;
	GWBUF* buff = NULL;
	unsigned char* ptr;
	switch(pkt){
	case PACKET_OK:
		psize = 11;
		break;

	default:
		break;

	}
	if(psize > 0){
		buff = gwbuf_alloc(psize);
		ptr = (unsigned char*)buff->start;
  
		switch(pkt){
		case PACKET_OK:

			ptr[0] = 7; /**Packet size*/
			ptr[1] = 0;
			ptr[2] = 0;

			ptr[3] = 1; /**sequence_id*/

			ptr[4] = 0; /**OK header*/

			ptr[5] = 0; /**affected_rows*/

			ptr[6] = 0; /**last_insert_id*/

			ptr[7] = 0; /**status_flags*/
			ptr[8] = 0;

			ptr[9] = 0; /**warnings*/
			ptr[10] = 0;
			break;

		default:
			break;

		}
	}
	return buff;
}


int process_opts(int argc, char** argv)
{
	int fd, buffsize = 1024;
	int rd,rdsz, rval = 0, error = 0;
	size_t fsize;
        char* saveptr;
	char *buff = calloc(buffsize,sizeof(char)), *tok = NULL;

	/**Parse 'harness.cnf' file*/

	if(buff == NULL){
		printf("Error: Call to malloc() failed.\n");
		return 1;
	}

	if((fd = open_file("harness.cnf",1)) < 0){
		printf("Failed to open configuration file.\n");
		free(buff);
		return 1;
	}

	
	if( (rval = lseek(fd,0,SEEK_END)) < 0 || 
		lseek(fd,0,SEEK_SET) < 0){
		printf("Error: Cannot seek file.\n");
		close(fd);
		free(buff);
		return 1;
	}

	fsize = (size_t)rval;

	instance.thrcount = 1;
	instance.session_count = 1;
	rdsz = read(fd,buff,fsize);
    buff[rdsz] = '\0';
	tok = strtok_r(buff,"=",&saveptr);
	while(tok){
		if(!strcmp(tok,"threads")){
			tok = strtok_r(NULL,"\n\0",&saveptr);
			instance.thrcount = strtol(tok,0,0);
		}else if(!strcmp(tok,"sessions")){
			tok = strtok_r(NULL,"\n\0",&saveptr);
			instance.session_count = strtol(tok,0,0);
		}
		tok = strtok_r(NULL,"=",&saveptr);
	}
  
  
   
	free(buff);
	instance.verbose = 1;

	if(argc < 2){
		close(fd);
		return 1;
	}

	char* conf_name = NULL;
	rval = 0;

	while((rd = getopt(argc,argv,"e:m:c:i:o:s:t:d:qh")) > 0){
		switch(rd){

		case 'e':
			instance.expected = open_file(optarg,0);
			printf("Expected output is read from: %s\n",optarg);
			break;

		case 'o':
			instance.outfile = open_file(optarg,1);
			printf("Output is written to: %s\n",optarg);
			break;

		case 'i':
			instance.infile = open_file(optarg,0);
			printf("Input is read from: %s\n",optarg);
			break;

		case 'c':
			if(conf_name){
				free(conf_name);
			}
			conf_name = strdup(optarg);
			printf("Configuration: %s\n",optarg);
			break;

		case 'q':
			instance.verbose = 0;
			break;

		case 's':
			instance.session_count = atoi(optarg);
			printf("Sessions: %i\n",instance.session_count);
			break;

		case 't':
			instance.thrcount = atoi(optarg);
			printf("Threads: %i\n",instance.thrcount);
			break;

		case 'd':
			instance.rt_delay = atoi(optarg);
			printf("Routing delay: %i ",instance.rt_delay);
			break;

		case 'h':
			printf(
				   "\nOptions for the configuration file 'harness.cnf'':\n\n"
				   "\tthreads\tNumber of threads to use when routing buffers\n"
				   "\tsessions\tNumber of sessions\n\n"
				   "Options for the command line:\n\n"
				   "\t-h\tDisplay this information\n"
				   "\t-c\tPath to the MaxScale configuration file to parse for filters\n"
				   "\t-i\tName of the input file for buffers\n"
				   "\t-o\tName of the output file for results\n"
				   "\t-q\tSuppress printing to stdout\n"
				   "\t-s\tNumber of sessions\n"
				   "\t-t\tNumber of threads\n"
				   "\t-d\tRouting delay\n");
			break;

		case 'm':
			instance.mod_dir = strdup(optarg);
			printf("Module directory: %s",optarg);
			break;

		default:
	
			break;

		}
	}
	printf("\n");

	if(conf_name && (error = load_config(conf_name))){
		load_query();
	}else{
		instance.running = 0;
	}
	free(conf_name);
	close(fd);

    if(!error)
    {
        rval = 1;
    }

	return rval;
}

int compare_files(int a,int b)
{
	char in[4098];
	char exp[4098];
	int line = 1;

	if(a < 1 || b < 1){
		return 1;
	}

	if(lseek(a,0,SEEK_SET) < 0 ||
	   lseek(b,0,SEEK_SET) < 0){
		return 1;
	}

	memset(in,0,4098);
	memset(exp,0,4098);

	while(fdgets(a,in,4098) && fdgets(b,exp,4098)){
		if(strcmp(in,exp)){
			printf("The files differ at line %d:\n%s\n-------------------------------------\n%s\n",line,in,exp);
			return 1;
		}
		line++;
	}
	return 0;
}

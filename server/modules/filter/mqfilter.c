/*
 * This file is distributed as part of MaxScale by SkySQL.  It is free
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
 * Copyright SkySQL Ab 2014
 */

/**
 * MQ Filter - AMQP Filter. 
 * A simple query logging filter that logs all the queries and
 * publishes them on to a RabbitMQ server.
 *
 * The filter makes no attempt to deal with query packets that do not fit
 * in a single GWBUF.
 * 
 * To use a SSL connection the CA certificate, the client certificate and the client public key must be provided.
 * By default this filter uses a TCP connection.
 *
 * The options for this filter are:
 * 	hostname	The server hostname where the messages are sent
 * 	port		Port to send the messages to
 * 	username	Server login username
 * 	password 	Server login password
 * 	vhost		The virtual host location on the server, where the messages are sent
 * 	exchange	The name of the exchange
 * 	exchange_type	The type of the exchange, defaults to direct
 * 	key		The routing key used when sending messages to the exchange
 * 	queue		The queue that will be bound to the used exchange
 * 	ssl_CA_cert	Path to the CA certificate in PEM format
 * 	ssl_client_cert Path to the client cerificate in PEM format
 * 	ssl_client_key	Path to the client public key in PEM format
 * 
 */
#include <stdio.h>
#include <fcntl.h>
#include <filter.h>
#include <modinfo.h>
#include <modutil.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <atomic.h>
#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>
#include <amqp_ssl_socket.h>
#include <log_manager.h>
#include <query_classifier.h>
#include <spinlock.h>
MODULE_INFO 	info = {
  MODULE_API_FILTER,
  MODULE_ALPHA_RELEASE,
  FILTER_VERSION,
  "A RabbitMQ query logging filter"
};

static char *version_str = "V1.0.2";
static int uid_gen;

/*
 * The filter entry points
 */
static	FILTER	*createInstance(char **options, FILTER_PARAMETER **);
static	void	*newSession(FILTER *instance, SESSION *session);
static	void 	closeSession(FILTER *instance, void *session);
static	void 	freeSession(FILTER *instance, void *session);
static	void	setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static	void	setUpstream(FILTER *instance, void *fsession, UPSTREAM *upstream);
static	int	routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static	int	clientReply(FILTER *instance, void *fsession, GWBUF *queue);
static	void	diagnostic(FILTER *instance, void *fsession, DCB *dcb);


static FILTER_OBJECT MyObject = {
  createInstance,
  newSession,
  closeSession,
  freeSession,
  setDownstream,
  setUpstream, 
  routeQuery,
  clientReply,
  diagnostic,
};

/**
 *Structure used to store messages and their properties.
 */
typedef struct mqmessage_t {
  amqp_basic_properties_t *prop;
  char *msg;
  struct mqmessage_t *next;
}mqmessage;

/**
 * A instance structure, containing the hostname, login credentials,
 * virtual host location and the names of the exchange and the key.
 * Also contains the paths to the CA certificate and client certificate and key.
 * 
 * Default values assume that a local RabbitMQ server is running on port 5672 with the default
 * user 'guest' and the password 'guest' using a default exchange named 'default_exchange' with a
 * routing key named 'key'. Type of the exchange is 'direct' by default. 
 * 
 */
typedef struct {
  int 	port; 
  char	*hostname; 
  char	*username; 
  char	*password; 
  char	*vhost; 
  char	*exchange;
  char	*exchange_type;
  char	*key;
  char	*queue;
  bool	use_ssl;
  char	*ssl_CA_cert;
  char	*ssl_client_cert;
  char 	*ssl_client_key;
  amqp_connection_state_t conn; /**The connection object*/
  amqp_socket_t* sock; /**The currently active socket*/
  amqp_channel_t channel; /**The current channel in use*/
  int conn_stat; /**state of the connection to the server*/
  int rconn_intv; /**delay for reconnects, in seconds*/
  time_t last_rconn; /**last reconnect attempt*/
  SPINLOCK* rconn_lock;
  mqmessage* messages;
} MQ_INSTANCE;

/**
 * The session structure for this MQ filter.
 * This stores the downstream filter information, such that the
 * filter is able to pass the query on to the next filter (or router)
 * in the chain.
 *
 * Also holds the necessary session connection information.
 *
 */
typedef struct {
  char*		uid; /**Unique identifier used to tag messages*/
  DOWNSTREAM	down;
  UPSTREAM	up;
  bool was_query; /**True if the previous routeQuery call had valid content*/
} MQ_SESSION;

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
  return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
FILTER_OBJECT *
GetModuleObject()
{
  return &MyObject;
}

/**
 * Internal function used to initialize the connection to 
 * the RabbitMQ server. Also used to reconnect to the server
 * in case the connection fails and to redeclare exchanges
 * and queues if they are lost
 * 
 */
static int 
init_conn(MQ_INSTANCE *my_instance)
{ 
  int rval = 0;
  int amqp_ok = AMQP_STATUS_OK;

  if(my_instance->use_ssl){

    if((my_instance->sock = amqp_ssl_socket_new(my_instance->conn)) != NULL){

      if((amqp_ok = amqp_ssl_socket_set_cacert(my_instance->sock,my_instance->ssl_CA_cert)) != AMQP_STATUS_OK){
	skygw_log_write(LOGFILE_ERROR,
			      "Error : Failed to set CA certificate: %s", amqp_error_string2(amqp_ok));
	goto cleanup;
      }
      if((amqp_ok = amqp_ssl_socket_set_key(my_instance->sock,
					    my_instance->ssl_client_cert,
					    my_instance->ssl_client_key)) != AMQP_STATUS_OK){
	skygw_log_write(LOGFILE_ERROR,
			      "Error : Failed to set client certificate and key: %s", amqp_error_string2(amqp_ok));
	goto cleanup;
      }
    }else{

      amqp_ok = AMQP_STATUS_SSL_CONNECTION_FAILED;
      skygw_log_write(LOGFILE_ERROR,
			    "Error : SSL socket creation failed.");
      goto cleanup;
    }

    /**SSL is not used, falling back to TCP*/
  }else if((my_instance->sock = amqp_tcp_socket_new(my_instance->conn)) == NULL){
    skygw_log_write(LOGFILE_ERROR,
			  "Error : TCP socket creation failed.");
    goto cleanup;    

  }

  /**Socket creation was successful, trying to open the socket*/
  if((amqp_ok = amqp_socket_open(my_instance->sock,my_instance->hostname,my_instance->port)) != AMQP_STATUS_OK){
    skygw_log_write(LOGFILE_ERROR,
			  "Error : Failed to open socket: %s", amqp_error_string2(amqp_ok));
    goto cleanup;
  }
  amqp_rpc_reply_t reply;
  reply = amqp_login(my_instance->conn,my_instance->vhost,0,AMQP_DEFAULT_FRAME_SIZE,0,AMQP_SASL_METHOD_PLAIN,my_instance->username,my_instance->password);
  if(reply.reply_type != AMQP_RESPONSE_NORMAL){
    skygw_log_write(LOGFILE_ERROR,
			  "Error : Login to RabbitMQ server failed.");
    
    goto cleanup;
  }
  amqp_channel_open(my_instance->conn,my_instance->channel);
  reply = amqp_get_rpc_reply(my_instance->conn);  
  if(reply.reply_type != AMQP_RESPONSE_NORMAL){
    skygw_log_write(LOGFILE_ERROR,
			  "Error : Channel creation failed.");
    goto cleanup;
  }

  amqp_exchange_declare(my_instance->conn,my_instance->channel,
			amqp_cstring_bytes(my_instance->exchange),
			amqp_cstring_bytes(my_instance->exchange_type),
			0, 1,
			amqp_empty_table);

  reply = amqp_get_rpc_reply(my_instance->conn);  

  if(reply.reply_type != AMQP_RESPONSE_NORMAL){
    skygw_log_write(LOGFILE_ERROR,
		    "Error : Exchange declaration failed,trying to redeclare the exchange.");
    if(reply.reply_type == AMQP_RESPONSE_SERVER_EXCEPTION){
      if(reply.reply.id == AMQP_CHANNEL_CLOSE_METHOD){
	amqp_send_method(my_instance->conn,my_instance->channel,AMQP_CHANNEL_CLOSE_OK_METHOD,NULL);
      }else if(reply.reply.id == AMQP_CONNECTION_CLOSE_METHOD){
	amqp_send_method(my_instance->conn,my_instance->channel,AMQP_CONNECTION_CLOSE_OK_METHOD,NULL);
      }
      
      my_instance->channel++;
      amqp_channel_open(my_instance->conn,my_instance->channel);
    
      amqp_exchange_delete(my_instance->conn,my_instance->channel,amqp_cstring_bytes(my_instance->exchange),0);
      amqp_exchange_declare(my_instance->conn,my_instance->channel,
			    amqp_cstring_bytes(my_instance->exchange),
			    amqp_cstring_bytes(my_instance->exchange_type),
			    0, 1,
			    amqp_empty_table);
      reply = amqp_get_rpc_reply(my_instance->conn);  
    }
    if(reply.reply_type != AMQP_RESPONSE_NORMAL){
      skygw_log_write(LOGFILE_ERROR,
		      "Error : Exchange redeclaration failed.");
      goto cleanup;
    }
  }

  if(my_instance->queue){

    

    amqp_queue_declare(my_instance->conn,my_instance->channel,
		       amqp_cstring_bytes(my_instance->queue),
		       0, 1, 0, 0,
		       amqp_empty_table);
    reply = amqp_get_rpc_reply(my_instance->conn);  
    if(reply.reply_type != AMQP_RESPONSE_NORMAL){
      skygw_log_write(LOGFILE_ERROR,
		      "Error : Queue declaration failed.");
      goto cleanup;
    }

 
    amqp_queue_bind(my_instance->conn,my_instance->channel,
		    amqp_cstring_bytes(my_instance->queue),
		    amqp_cstring_bytes(my_instance->exchange),
		    amqp_cstring_bytes(my_instance->key),
		    amqp_empty_table);
    reply = amqp_get_rpc_reply(my_instance->conn);  
    if(reply.reply_type != AMQP_RESPONSE_NORMAL){
      skygw_log_write(LOGFILE_ERROR,
		      "Error : Failed to bind queue to exchange.");
      goto cleanup;
    }
  }
  rval = 1;

 cleanup:

  return rval;
 
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 * 
 * @param options	The options for this filter
 *
 * @return The instance data for this new instance
 */
static	FILTER	*
createInstance(char **options, FILTER_PARAMETER **params)
{
  MQ_INSTANCE	*my_instance;
  
  if ((my_instance = calloc(1, sizeof(MQ_INSTANCE)))&& 
      (my_instance->rconn_lock = malloc(sizeof(SPINLOCK))))
    {
      spinlock_init(my_instance->rconn_lock);
      uid_gen = 0;

      if((my_instance->conn = amqp_new_connection()) == NULL){


	return NULL;
      }
      my_instance->channel = 1;
      my_instance->last_rconn = time(NULL);
      my_instance->conn_stat = AMQP_STATUS_OK;
      my_instance->rconn_intv = 1;
      my_instance->port = 5672;

      int i;
      for(i = 0;params[i];i++){
	if(!strcmp(params[i]->name,"hostname")){
	  my_instance->hostname = strdup(params[i]->value);
	}else if(!strcmp(params[i]->name,"username")){
	  my_instance->username = strdup(params[i]->value);
	}else if(!strcmp(params[i]->name,"password")){
	  my_instance->password = strdup(params[i]->value);
	}else if(!strcmp(params[i]->name,"vhost")){
	  my_instance->vhost = strdup(params[i]->value);
	}else if(!strcmp(params[i]->name,"port")){
	  my_instance->port = atoi(params[i]->value);
	}else if(!strcmp(params[i]->name,"exchange")){
	  my_instance->exchange = strdup(params[i]->value);  
	}else if(!strcmp(params[i]->name,"key")){
	  my_instance->key = strdup(params[i]->value);
	}else if(!strcmp(params[i]->name,"queue")){
	  my_instance->queue = strdup(params[i]->value);
	}
	else if(!strcmp(params[i]->name,"ssl_client_certificate")){
	  my_instance->ssl_client_cert = strdup(params[i]->value);
	}
	else if(!strcmp(params[i]->name,"ssl_client_key")){
	  my_instance->ssl_client_key = strdup(params[i]->value);
	}
	else if(!strcmp(params[i]->name,"ssl_CA_cert")){
	  my_instance->ssl_CA_cert = strdup(params[i]->value);
	}else if(!strcmp(params[i]->name,"exchange_type")){
	  my_instance->exchange_type = strdup(params[i]->value);
	}
      }
      
      if(my_instance->hostname == NULL){
	my_instance->hostname = strdup("localhost");	
      }
      if(my_instance->username == NULL){
	my_instance->username = strdup("guest");	
      }
      if(my_instance->password == NULL){
	my_instance->password = strdup("guest");	
      }
      if(my_instance->vhost == NULL){
	my_instance->vhost = strdup("/");	
      }
      if(my_instance->exchange == NULL){
	my_instance->exchange = strdup("default_exchange");	
      }
      if(my_instance->key == NULL){
	my_instance->key = strdup("key");
      }
      if(my_instance->exchange_type == NULL){
	my_instance->exchange_type = strdup("direct");
      }

      if(my_instance->ssl_client_cert != NULL &&
	 my_instance->ssl_client_key != NULL &&
	 my_instance->ssl_CA_cert != NULL){
	my_instance->use_ssl = true;
      }else{
	my_instance->use_ssl = false;
      }
      
      if(my_instance->use_ssl){
	amqp_set_initialize_ssl_library(0);/**Assume the underlying SSL library is already initialized*/
      }
      
      /**Connect to the server*/
      init_conn(my_instance);

    }
  return (FILTER *)my_instance;
}



/**
 * Declares a persistent, non-exclusive and non-passive queue that
 * auto-deletes after all the messages have been consumed.
 * @param my_session MQ_SESSION instance used to declare the queue
 * @param qname Name of the queue to be declared
 * @return Returns 0 if an error occurred, 1 if successful
 */
int declareQueue(MQ_INSTANCE	*my_instance, MQ_SESSION* my_session, char* qname)
{
  int success = 1;
  amqp_rpc_reply_t reply;

  spinlock_acquire(my_instance->rconn_lock);

  amqp_queue_declare(my_instance->conn,my_instance->channel,
		     amqp_cstring_bytes(qname),
		     0, 1, 0, 1,
		     amqp_empty_table);
  reply = amqp_get_rpc_reply(my_instance->conn);  
  if(reply.reply_type != AMQP_RESPONSE_NORMAL){
    success = 0;
    skygw_log_write(LOGFILE_ERROR,
		    "Error : Queue declaration failed.");
   
  }

 
  amqp_queue_bind(my_instance->conn,my_instance->channel,
		  amqp_cstring_bytes(qname),
		  amqp_cstring_bytes(my_instance->exchange),
		  amqp_cstring_bytes(my_session->uid),
		  amqp_empty_table);
  reply = amqp_get_rpc_reply(my_instance->conn);  
  if(reply.reply_type != AMQP_RESPONSE_NORMAL){
    success = 0;
    skygw_log_write(LOGFILE_ERROR,
		    "Error : Failed to bind queue to exchange.");
   
  }
    spinlock_release(my_instance->rconn_lock);        
    return success;
}

/**
 * Broadcasts a message on the message stack to the RabbitMQ server
 * and frees the allocated memory if successful.
 * @return AMQP_STATUS_OK if the broadcasting was successful
 */
int sendMessage(MQ_INSTANCE *instance)
{
  int err_code;
  mqmessage *tmp;

 if(instance->conn_stat != AMQP_STATUS_OK){

    if(difftime(time(NULL),instance->last_rconn) > instance->rconn_intv){

      instance->last_rconn = time(NULL);

      if(init_conn(instance)){
	instance->rconn_intv = 1.0;
	instance->conn_stat = AMQP_STATUS_OK;	

      }else{
	instance->rconn_intv += 5.0;
	skygw_log_write(LOGFILE_ERROR,
			"Error : Failed to reconnect to the MQRabbit server ");
      }
    }
  }

  if(instance->messages){
    instance->conn_stat = amqp_basic_publish(instance->conn,instance->channel,
				  amqp_cstring_bytes(instance->exchange),
				  amqp_cstring_bytes(instance->key),
				  0,0,instance->messages->prop,amqp_cstring_bytes(instance->messages->msg));


    /**Message was sent successfully*/
    if(instance->conn_stat == AMQP_STATUS_OK){
      tmp = instance->messages;
      instance->messages = instance->messages->next;
      free(tmp->prop);
      free(tmp->msg);
      free(tmp);
    }
    
  }

  err_code = instance->conn_stat;

  return err_code;
}


/**
 * Push a new message on the stack to be broadcasted later.
 * The message assumes ownership of the memory allocated to the message content and properties.
 * @param prop Message properties
 * @param msg Message content
 */
void pushMessage(MQ_INSTANCE *instance, amqp_basic_properties_t* prop, char* msg)
{
  spinlock_acquire(instance->rconn_lock);
  
  mqmessage* newmsg = malloc(sizeof(mqmessage));
  if(newmsg){
    newmsg->msg = msg;
    newmsg->prop = prop;
    newmsg->next = NULL;
    
    if(instance->messages){
      newmsg->next = instance->messages;
    }

    instance->messages = newmsg;
    
  }else{
    skygw_log_write(LOGFILE_ERROR,
		    "Error : Cannot allocate enough memory.");
    free(prop);
    free(msg);
  }

  while(instance->messages){
    if(sendMessage(instance) != AMQP_STATUS_OK){
      break;
    }
  }

  spinlock_release(instance->rconn_lock);        
}



/**
 * Associate a new session with this instance of the filter and opens
 * a connection to the server and prepares the exchange and the queue for use.
 *
 *
 * @param instance	The filter instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(FILTER *instance, SESSION *session)
{
  MQ_SESSION	*my_session;
  
  if ((my_session = calloc(1, sizeof(MQ_SESSION))) != NULL){

    my_session->was_query = false;
    my_session->uid = NULL;

  }

  return my_session;
}



/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 * In the case of the MQ filter we do nothing.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static	void 	
closeSession(FILTER *instance, void *session)
{
}

/**
 * Free the memory associated with the session
 *
 * @param instance	The filter instance
 * @param session	The filter session
 */
static void
freeSession(FILTER *instance, void *session)
{
  MQ_SESSION	*my_session = (MQ_SESSION *)session;
  free(my_session->uid);
  free(my_session);
  return;
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance	The filter instance data
 * @param session	The filter session 
 * @param downstream	The downstream filter or router.
 */
static void
setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
  MQ_SESSION	*my_session = (MQ_SESSION *)session;
  my_session->down = *downstream;
}

static	void	setUpstream(FILTER *instance, void *session, UPSTREAM *upstream)
{
  MQ_SESSION	*my_session = (MQ_SESSION *)session;
  my_session->up = *upstream;
}


/**
 * Generates a unique key using a number of unique unsigned integers.
 * @param array The array that is used
 * @param size Size of the array
 */
void genkey(char* array, int size)
{
  int i = 0;
  for(i = 0;i<size;i += 4){
    sprintf(array+i,"%04x",atomic_add(&uid_gen,1));
  }
  sprintf(array+i,"%0*x",size - i,atomic_add(&uid_gen,1));
}



/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * The function tries to extract a SQL query out of the query buffer,
 * adds a timestamp to it and publishes the resulting string on the exchange.
 * The message is tagged with an unique identifier and the clientReply will
 * use the same identifier for the reply from the backend.
 * 
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static	int	
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
  MQ_SESSION	*my_session = (MQ_SESSION *)session;
  MQ_INSTANCE	*my_instance = (MQ_INSTANCE *)instance;
  char		*ptr, t_buf[128], *combined,*canon_q;
  bool		success = true;
  int		length;
  amqp_basic_properties_t *prop;
  
  if(modutil_is_SQL(queue)){

    if(my_session->uid == NULL){

      my_session->uid = calloc(33,sizeof(char));

      if(!my_session->uid){
	skygw_log_write(LOGFILE_ERROR,"Error : Out of memory.");
      }else{
	genkey(my_session->uid,32);
      }

    }
    
  }

  if(modutil_extract_SQL(queue, &ptr, &length)){

    my_session->was_query = true;
      
    if((prop = malloc(sizeof(amqp_basic_properties_t)))){
      prop->_flags = AMQP_BASIC_CONTENT_TYPE_FLAG |
	AMQP_BASIC_DELIVERY_MODE_FLAG |
	AMQP_BASIC_MESSAGE_ID_FLAG | 
	AMQP_BASIC_CORRELATION_ID_FLAG;
      prop->content_type = amqp_cstring_bytes("text/plain");
      prop->delivery_mode = AMQP_DELIVERY_PERSISTENT;
      prop->correlation_id = amqp_cstring_bytes(my_session->uid);
      prop->message_id = amqp_cstring_bytes("query");
    }

    if (!query_is_parsed(queue)){
      success = parse_query(queue);
    }

    if(success){
      
      /**Try to convert to a canonical form and use the plain query if unsuccessful*/
      if((canon_q = skygw_get_canonical(queue)) == NULL){
	parsing_info_t* pinfo = (parsing_info_t*)queue->gwbuf_parsing_info;
	canon_q = strdup(pinfo->pi_query_plain_str);
      }

    }
 
    memset(t_buf,0,128);      
    sprintf(t_buf, "%lu|",(unsigned long)time(NULL));

    int qlen = strnlen(canon_q,length) + strnlen(t_buf,128);
    if((combined = malloc((qlen+1)*sizeof(char))) == NULL){
      skygw_log_write_flush(LOGFILE_ERROR,
			    "Error : Out of memory");
    }
    strcpy(combined,t_buf);
    strncat(combined,canon_q,length);
      
    pushMessage(my_instance,prop,combined);
    free(canon_q);
  }
  
  /** Pass the query downstream */
  return my_session->down.routeQuery(my_session->down.instance,
				     my_session->down.session, queue);
}

/**
 * Calculated the length of the SQL packet.
 * @param c Pointer to the first byte of a packet
 * @return The length of the packet
 */
unsigned int pktlen(void* c)
{
  unsigned char* ptr = (unsigned char*)c;
  unsigned int plen = *ptr;
  plen += (*++ptr << 8);
  plen += (*++ptr << 8);
  return plen;
}

/**
 * Converts a length-encoded integer to an unsigned integer as defined by the
 * MySQL manual.
 * @param c Pointer to the first byte of a length-encoded integer
 * @return The value converted to a standard unsigned integer
 */
unsigned int leitoi(unsigned char* c)
{
  unsigned char* ptr = c;
  unsigned int sz = *ptr;
  if(*ptr  < 0xfb) return sz;
  if(*ptr == 0xfc){
    sz = *++ptr;
    sz += (*++ptr << 8);
  }else if(*ptr == 0xfd){
    sz = *++ptr;
    sz += (*++ptr << 8);
    sz += (*++ptr << 8);
  }else{
    sz = *++ptr;
    sz += (*++ptr << 8);
    sz += (*++ptr << 8);
    sz += (*++ptr << 8);
    sz += (*++ptr << 8);
    sz += (*++ptr << 8);
    sz += (*++ptr << 8);
    sz += (*++ptr << 8);
    sz += (*++ptr << 8);
  }
  return sz;
}

/**
 * Converts a length-encoded integer into a standard unsigned integer 
 * and advances the pointer to the next unrelated byte.
 *
 * @param c Pointer to the first byte of a length-encoded integer
 */
unsigned int consume_leitoi(unsigned char** c)
{
  unsigned int rval = leitoi(*c);
  if(**c == 0xfc){
    *c += 3;
  }else if(**c == 0xfd){
    *c += 4;
  }else if(**c == 0xfe){
    *c += 9;
  }else{
    *c += 1;
  }
  return rval;
}

/**
 * Converts length-encoded strings to character strings and advanced the pointer to the next unrelated byte.
 * The caller is responsible for freeing the allocated memory.
 * @param c Pointer to the first byte of a valid packet.
 * @return The newly allocated string or NULL of an error occurred
 */
char* consume_lestr(unsigned char** c)
{
  unsigned int slen = consume_leitoi(c);
  char *str = calloc((slen + 1), sizeof(char));
  if(str){
    memcpy(str,*c,slen);
    *c += slen;
  }
  return str;
}

/**
 *Checks whether the packet is an EOF packet.
 * @param p Pointer to the first byte of a packet
 * @return 1 if the packet is an EOF packet and 0 if it is not
 */
unsigned int is_eof(void* p)
{
  unsigned char* ptr = (unsigned char*) p;
  return *(ptr) == 0x05 && *(ptr + 1) == 0x00 &&  *(ptr + 2) == 0x00 &&  *(ptr + 4) == 0xfe;
}


/**
 * The clientReply entry point. This is passed the response buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the upstream component
 * (filter or router) in the filter chain.
 *
 * The function tries to extract a SQL query response out of the response buffer,
 * adds a timestamp to it and publishes the resulting string on the exchange.
 * The message is tagged with the same identifier that the query was.
 * 
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param reply		The response data
 */
static int clientReply(FILTER* instance, void *session, GWBUF *reply)
{
  MQ_SESSION		*my_session = (MQ_SESSION *)session;
  MQ_INSTANCE		*my_instance = (MQ_INSTANCE *)instance;
  char			t_buf[128],*combined;
  unsigned int		pkt_len = pktlen(reply->sbuf->data), offset = 0;
  amqp_basic_properties_t *prop;

  if (my_session->was_query){

    int packet_ok = 0, was_last = 0;

    my_session->was_query = false;

    if(pkt_len > 0){
      if((prop = malloc(sizeof(amqp_basic_properties_t)))){
	prop->_flags = AMQP_BASIC_CONTENT_TYPE_FLAG |
	  AMQP_BASIC_DELIVERY_MODE_FLAG |
	  AMQP_BASIC_MESSAGE_ID_FLAG | 
	  AMQP_BASIC_CORRELATION_ID_FLAG;
	prop->content_type = amqp_cstring_bytes("text/plain");
	prop->delivery_mode = AMQP_DELIVERY_PERSISTENT;
	prop->correlation_id = amqp_cstring_bytes(my_session->uid);
	prop->message_id = amqp_cstring_bytes("reply");
      }
      if(!(combined = calloc(GWBUF_LENGTH(reply) + 256,sizeof(char)))){
	skygw_log_write_flush(LOGFILE_ERROR,
			      "Error : Out of memory");
      }

      memset(t_buf,0,128);
      sprintf(t_buf,"%lu|",(unsigned long)time(NULL));
      
      
      memcpy(combined + offset,t_buf,strnlen(t_buf,40));
      offset += strnlen(t_buf,40);

      if(*(reply->sbuf->data + 4) == 0x00){ /**OK packet*/
	unsigned int aff_rows = 0, l_id = 0, s_flg = 0, wrn = 0;
	unsigned char *ptr = (unsigned char*)(reply->sbuf->data + 5);
	pkt_len = pktlen(reply->sbuf->data);
	aff_rows = consume_leitoi(&ptr);
	l_id = consume_leitoi(&ptr);
	s_flg |= *ptr++;
	s_flg |= (*ptr++ << 8);
	wrn |= *ptr++;
	wrn |= (*ptr++ << 8);
	sprintf(combined + offset,"OK - affected_rows: %d "
		" last_insert_id: %d "
		" status_flags: %#0x "
		" warnings: %d ",		
		aff_rows,l_id,s_flg,wrn);
	offset += strnlen(combined,GWBUF_LENGTH(reply) + 256) - offset;

	if(pkt_len > 7){
	  int plen = consume_leitoi(&ptr);
	  if(plen > 0){
	    sprintf(combined + offset," message: %.*s\n",plen,ptr);
	  }
	}

	packet_ok = 1;
	was_last = 1;

      }else if(*(reply->sbuf->data + 4) == 0xff){ /**ERR packet*/

	sprintf(combined + offset,"ERROR - message: %.*s",
		(int)(reply->end - ((void*)(reply->sbuf->data + 13))),
		(char *)reply->sbuf->data + 13);
	packet_ok = 1;
	was_last = 1;
    
      }else if(*(reply->sbuf->data + 4) == 0xfb){ /**LOCAL_INFILE request packet*/
      
	unsigned char	*rset = (unsigned char*)reply->sbuf->data;
	strcpy(combined + offset,"LOCAL_INFILE: ");
	strncat(combined + offset,(const char*)rset+5,pktlen(rset));
	packet_ok = 1;
	was_last = 1;
      
      }else{ /**Result set*/
      
	unsigned char	*rset = (unsigned char*)(reply->sbuf->data + 4);
	char		*tmp;
	unsigned int	col_cnt = consume_leitoi(&rset);

	tmp = calloc(256,sizeof(char));
	sprintf(tmp,"Columns: %d",col_cnt);
	memcpy(combined + offset,tmp,strnlen(tmp,256));
	offset += strnlen(tmp,256);
	memcpy(combined + offset,"\n",1);
	offset++;
	free(tmp);
       
	packet_ok = 1;
	was_last = 1;
	
      }
      if(packet_ok){

	pushMessage(my_instance,prop,combined);

	if(was_last){

	  /**Successful reply received and sent, releasing uid*/
	  
	  free(my_session->uid);
	  my_session->uid = NULL;

	} 
      }
    }

  }

  return my_session->up.clientReply(my_session->up.instance,
				    my_session->up.session, reply);
}

/**
 * Diagnostics routine
 *
 * Prints the connection details and the names of the exchange,
 * queue and the routing key.
 *
 * @param	instance	The filter instance
 * @param	fsession	Filter session, may be NULL
 * @param	dcb		The DCB for diagnostic output
 */
static	void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
  MQ_INSTANCE	*my_instance = (MQ_INSTANCE *)instance;

  if (my_instance)
    {
      dcb_printf(dcb, "\t\tConnecting to %s:%d as %s/%s.\nVhost: %s\tExchange: %s\tKey: %s\tQueue: %s\n",
		 my_instance->hostname,my_instance->port,
		 my_instance->username,my_instance->password,
		 my_instance->vhost, my_instance->exchange,
		 my_instance->key, my_instance->queue
		 );
    }
}
	

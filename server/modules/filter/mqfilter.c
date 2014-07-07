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
 * MQ Filter - AMQP Filter. A primitive query logging filter, simply
 * used to verify the filter mechanism for downstream filters. All queries
 * that are passed through the filter will be sent to a RabbitMQ server.
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
 * 	exchange	The name of the exchange used for all messages
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
#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>
#include <amqp_ssl_socket.h>
#include <log_manager.h>
MODULE_INFO 	info = {
  MODULE_API_FILTER,
  MODULE_ALPHA_RELEASE,
  FILTER_VERSION,
  "A RabbitMQ query logging filter"
};

static char *version_str = "V1.0.0";

/*
 * The filter entry points
 */
static	FILTER	*createInstance(char **options, FILTER_PARAMETER **);
static	void	*newSession(FILTER *instance, SESSION *session);
static	void 	closeSession(FILTER *instance, void *session);
static	void 	freeSession(FILTER *instance, void *session);
static	void	setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static	int	routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static	void	diagnostic(FILTER *instance, void *fsession, DCB *dcb);


static FILTER_OBJECT MyObject = {
  createInstance,
  newSession,
  closeSession,
  freeSession,
  setDownstream,
  routeQuery,
  diagnostic,
};

/**
 * A instance structure, containing the hostname, login credentials,
 * virtual host location and the names of the exchange and the key.
 * Also contains the paths to the CA certificate and client certificate and key.
 * 
 * Default values assume that a local RabbitMQ server is running on port 5672 with the default
 * user 'guest' and the password 'guest' using a default exchange named 'default_exchange' with a
 * routing key named 'key'. A queue named 'default_queue' is bound to the used exchange. 
 * 
 */
typedef struct {
  int 	port; 
  amqp_channel_t channel; /**The channel number of the previous session*/
  char	*hostname; 
  char	*username; 
  char	*password; 
  char	*vhost; 
  char	*exchange;
  char	*key;
  char	*queue;
  int	use_ssl;
  char	*ssl_CA_cert;
  char	*ssl_client_cert;
  char 	*ssl_client_key;
  int conn_stat; /**state of the connection to the server*/
  int rconn_intv; /**delay for reconnects*/
  time_t last_rconn; /**last reconnect attempt*/
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
  DOWNSTREAM	down;
  amqp_connection_state_t conn; /**The connection object*/
  amqp_socket_t* sock; /**The currently active socket*/
  amqp_channel_t channel; /**The current channel in use*/
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
  
  if ((my_instance = calloc(1, sizeof(MQ_INSTANCE))) != NULL)
    {
      my_instance->hostname = NULL;
      my_instance->username = NULL;
      my_instance->password = NULL;
      my_instance->vhost = NULL;
      my_instance->port = 0;
      my_instance->exchange = NULL;
      my_instance->key = NULL;
      my_instance->queue = NULL;
      my_instance->use_ssl = 0;
      my_instance->ssl_CA_cert = NULL;
      my_instance->ssl_client_cert = NULL;
      my_instance->ssl_client_key = NULL;
      my_instance->channel = 1;
      my_instance->last_rconn = time(NULL);
      my_instance->conn_stat = AMQP_STATUS_OK;
      my_instance->rconn_intv = 1;
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
      if(my_instance->queue == NULL){
	my_instance->queue = strdup("default_queue");	
      }
      if(my_instance->key == NULL){
	my_instance->key = strdup("key");
      }
      if(my_instance->port == 0){
	my_instance->port = 5672;	
      }
      if(my_instance->ssl_client_cert != NULL &&
	 my_instance->ssl_client_key != NULL &&
	 my_instance->ssl_CA_cert != NULL){
	my_instance->use_ssl = 1;
      }
      
      if(my_instance->use_ssl){
	amqp_set_initialize_ssl_library(0);/**Assume the underlying SSL library is already initialized*/
      }

    }
  return (FILTER *)my_instance;
}

/**
 * Internal function used to initialize the connection to 
 * the RabbitMQ server. Also used to reconnect to the server
 * in case the connection fails and to redeclare exchanges
 * and queues if they are lost
 * 
 */
static int 
init_conn(MQ_INSTANCE *my_instance, MQ_SESSION *my_session)
{ 
 
  int amqp_ok = AMQP_STATUS_OK;

  if(my_instance->use_ssl){

    if((my_session->sock = amqp_ssl_socket_new(my_session->conn)) != NULL){

      if((amqp_ok = amqp_ssl_socket_set_cacert(my_session->sock,my_instance->ssl_CA_cert)) != AMQP_STATUS_OK){
	skygw_log_write(LOGFILE_ERROR,
			      "Error : Failed to set CA certificate: %s", amqp_error_string2(amqp_ok));
	return 0;
      }
      if((amqp_ok = amqp_ssl_socket_set_key(my_session->sock,
				 my_instance->ssl_client_cert,
					    my_instance->ssl_client_key)) != AMQP_STATUS_OK){
	skygw_log_write(LOGFILE_ERROR,
			      "Error : Failed to set client certificate and key: %s", amqp_error_string2(amqp_ok));
	return 0;
      }
    }else{

      amqp_ok = AMQP_STATUS_SSL_CONNECTION_FAILED;
      skygw_log_write(LOGFILE_ERROR,
			    "Error : SSL socket creation failed.");
      return 0;
    }

    /**SSL is not used, falling back to TCP*/
  }else if((my_session->sock = amqp_tcp_socket_new(my_session->conn)) == NULL){
    skygw_log_write(LOGFILE_ERROR,
			  "Error : TCP socket creation failed.");
    return 0;    

  }

  /**Socket creation was successful, trying to open the socket*/
  if((amqp_ok = amqp_socket_open(my_session->sock,my_instance->hostname,my_instance->port)) != AMQP_STATUS_OK){
    skygw_log_write(LOGFILE_ERROR,
			  "Error : Failed to open socket: %s", amqp_error_string2(amqp_ok));
    return 0;    
  }
  amqp_rpc_reply_t reply;
  reply = amqp_login(my_session->conn,my_instance->vhost,0,AMQP_DEFAULT_FRAME_SIZE,0,AMQP_SASL_METHOD_PLAIN,my_instance->username,my_instance->password);
  if(reply.reply_type != AMQP_RESPONSE_NORMAL){
    skygw_log_write(LOGFILE_ERROR,
			  "Error : Login to RabbitMQ server failed.");
    return 0;
  }
  my_session->channel = my_instance->channel++;
  amqp_channel_open(my_session->conn,my_session->channel);
  reply = amqp_get_rpc_reply(my_session->conn);  
  if(reply.reply_type != AMQP_RESPONSE_NORMAL){
    skygw_log_write(LOGFILE_ERROR,
		    "Error : Channel creation failed.");
    return 0;
  }

  amqp_exchange_declare(my_session->conn,my_session->channel,
			amqp_cstring_bytes(my_instance->exchange),
			amqp_cstring_bytes("fanout"),
			0, 1,
			amqp_empty_table);
  reply = amqp_get_rpc_reply(my_session->conn);  
  if(reply.reply_type != AMQP_RESPONSE_NORMAL){
    skygw_log_write(LOGFILE_ERROR,
		    "Error : Exchange declaration failed.");
    return 0;
  }

  amqp_queue_declare(my_session->conn,my_session->channel,
		     amqp_cstring_bytes(my_instance->queue),
		     0, 1, 0, 0,
		     amqp_empty_table);
  reply = amqp_get_rpc_reply(my_session->conn);  
  if(reply.reply_type != AMQP_RESPONSE_NORMAL){
    skygw_log_write(LOGFILE_ERROR,
		    "Error : Queue declaration failed.");
    return 0;
  }
  amqp_queue_bind(my_session->conn,my_session->channel,
		  amqp_cstring_bytes(my_instance->queue),
		  amqp_cstring_bytes(my_instance->exchange),
		  amqp_cstring_bytes(my_instance->key),
		  amqp_empty_table);
  reply = amqp_get_rpc_reply(my_session->conn);  
  if(reply.reply_type != AMQP_RESPONSE_NORMAL){
    skygw_log_write(LOGFILE_ERROR,
		    "Error : Failed to bind queue to exchange.");
    return 0;
  }
  return 1;
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
  MQ_INSTANCE	*my_instance = (MQ_INSTANCE *)instance;
  MQ_SESSION	*my_session;
  
  if ((my_session = calloc(1, sizeof(MQ_SESSION))) != NULL){

    if((my_session->conn = amqp_new_connection()) == NULL){
      free(my_session);
      return NULL;
    }

    init_conn(my_instance,my_session);
    
  }

  return my_session;
}



/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 * In the case of the MQ filter we simply close the connection to the server.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static	void 	
closeSession(FILTER *instance, void *session)
{
  MQ_SESSION	*my_session = (MQ_SESSION *)session;
  amqp_channel_close(my_session->conn,my_session->channel,AMQP_REPLY_SUCCESS);
  amqp_connection_close(my_session->conn,AMQP_REPLY_SUCCESS);
  amqp_destroy_connection(my_session->conn);
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

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * The function tries to extract a SQL query out of the query buffer,
 * adds a timestamp to it and publishes the resulting string on the exchange.
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
  char		*ptr, t_buf[40], *combined;
  int		length, error_code = AMQP_STATUS_OK;
  struct tm	t;
  struct timeval	tv;
  amqp_basic_properties_t prop;

  if(my_instance->conn_stat != AMQP_STATUS_OK && 
     (difftime(time(NULL),my_instance->last_rconn) > my_instance->rconn_intv))
    {

      if(init_conn(my_instance,my_session)){

	my_instance->rconn_intv = 1;
	my_instance->conn_stat = AMQP_STATUS_OK;

      }else{
	
	my_instance->rconn_intv *= 2;
	skygw_log_write(LOGFILE_ERROR,
			"Error : Failed to reconnect to the MQRabbit server ");
      }

      my_instance->last_rconn = time(NULL);

    }

  prop._flags = AMQP_BASIC_CONTENT_TYPE_FLAG|AMQP_BASIC_DELIVERY_MODE_FLAG;
  prop.content_type = amqp_cstring_bytes("text/plain");
  prop.delivery_mode = AMQP_DELIVERY_PERSISTENT;

  if (modutil_extract_SQL(queue, &ptr, &length))
    {
      
      gettimeofday(&tv, NULL);
      localtime_r(&tv.tv_sec, &t);
      sprintf(t_buf, "%02d:%02d:%02d.%-3d %d/%02d/%d, ",
	      t.tm_hour, t.tm_min, t.tm_sec, (int)(tv.tv_usec / 1000),
	      t.tm_mday, t.tm_mon + 1, 1900 + t.tm_year);

      int qlen = length + strnlen(t_buf,40);
      if((combined = malloc((qlen+1)*sizeof(char))) == NULL){
	skygw_log_write(LOGFILE_ERROR,
			"Error : Out of memory");
      }
      strcpy(combined,t_buf);
      strncat(combined,ptr,length);

      if((my_instance->conn_stat = amqp_basic_publish(my_session->conn,my_session->channel,
						      amqp_cstring_bytes(my_instance->exchange),
						      amqp_cstring_bytes(my_instance->key),
						      0,0,&prop,amqp_cstring_bytes(combined))
	  ) != AMQP_STATUS_OK){

	skygw_log_write(LOGFILE_ERROR,
			"Error : Failed to publish message to MQRabbit server: "
			"%s",amqp_error_string2(error_code));

	/**Connection error, try to reconnect and republish immediately*/
	if(init_conn(my_instance,my_session)){
	  my_instance->conn_stat =  amqp_basic_publish(my_session->conn,my_session->channel,
						       amqp_cstring_bytes(my_instance->exchange),
						       amqp_cstring_bytes(my_instance->key),
						       0,0,&prop,amqp_cstring_bytes(combined));
	  my_instance->rconn_intv = 1;
	}else{
	  my_instance->rconn_intv *= 2;
	}
	my_instance->last_rconn = time(NULL);
      } 

    }
  /** Pass the query downstream */
  return my_session->down.routeQuery(my_session->down.instance,
				     my_session->down.session, queue);
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
	

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
 * QLA Filter - Query Log All. A primitive query logging filter, simply
 * used to verify the filter mechanism for downstream filters. All queries
 * that are passed through the filter will be written to file.
 *
 * The filter makes no attempt to deal with query packets that do not fit
 * in a single GWBUF.
 *
 * A single option may be passed to the filter, this is the name of the
 * file to which the queries are logged. A serial number is appended to this
 * name in order that each session logs to a different file.
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

MODULE_INFO 	info = {
  MODULE_API_FILTER,
  MODULE_ALPHA_RELEASE,
  FILTER_VERSION,
  "A simple query logging filter"
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
 * A instance structure, the assumption is that the option passed
 * to the filter is simply a base for the filename to which the queries
 * are logged.
 *
 * To this base a session number is attached such that each session will
 * have a nique name.
 */
typedef struct {
  int	sessions;
  int 	port;
  int 	logfd;
  char	*hostname;
  char	*username;
  char	*password;
  char	*vhost;
} MQ_INSTANCE;

/**
 * The session structure for this QLA filter.
 * This stores the downstream filter information, such that the
 * filter is able to pass the query on to the next filter (or router)
 * in the chain.
 *
 * It also holds the file descriptor to which queries are written.
 */
typedef struct {
  DOWNSTREAM	down;
  amqp_connection_state_t conn;
  amqp_socket_t* sock;
  amqp_channel_t channel;
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
      /*if (options){
	my_instance->hostname = strdup(options[0]);
      }
      else{
	my_instance->hostname = strdup("localhost");
      }*/
      my_instance->hostname = strdup("localhost");
      my_instance->username = strdup("guest");
      my_instance->password = strdup("guest");
      my_instance->vhost = strdup("/");
      my_instance->sessions = 0;
      my_instance->port = 5672;      
    }
  return (FILTER *)my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * Create the file to log to and open it.
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
  
  if(my_instance->sessions < 1){
   my_instance->logfd = open("/tmp/mqfilterlog", O_WRONLY|O_CREAT,0666); 
  }
  char* msg;
  msg = calloc(128,sizeof(char));
  if ((my_session = calloc(1, sizeof(MQ_SESSION))) != NULL)
    {
      my_session->conn = amqp_new_connection();
  my_session->sock = amqp_tcp_socket_new(my_session->conn);
   if(my_session->sock == NULL){
    amqp_rpc_reply_t rpl = amqp_get_rpc_reply(my_session->conn);    
    strcpy(msg,amqp_error_string2(rpl.library_error));
    write(my_instance->logfd,msg,strlen(msg));
    }
    int err_num;    
    if((err_num = amqp_socket_open(my_session->sock,my_instance->hostname,my_instance->port)) != AMQP_STATUS_OK){
      strcpy(msg,"Error opening socket: ");
      write(my_instance->logfd,msg,strlen(msg));
      strcpy(msg,amqp_error_string2(err_num));
      write(my_instance->logfd,msg,strlen(msg));
    }else{
      amqp_login(my_session->conn,my_instance->vhost,0,131072,0,AMQP_SASL_METHOD_PLAIN,my_instance->username,my_instance->password);
      my_session->channel = 1;
      amqp_channel_open(my_session->conn,my_session->channel);
      amqp_queue_declare(my_session->conn,1,amqp_cstring_bytes("testqueue"),0,1,0,0,amqp_empty_table);
      strcpy(msg,"Logged in successfully.\n");
      write(my_instance->logfd,msg,strlen(msg));
      my_instance->sessions++;
    }
    }
    free(msg);
  return my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 * In the case of the QLA filter we simple close the file descriptor.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static	void 	
closeSession(FILTER *instance, void *session)
{
  MQ_SESSION	*my_session = (MQ_SESSION *)session;
  MQ_INSTANCE *my_instance = (MQ_INSTANCE *)instance;
  char *msg;
  msg = calloc(128,sizeof(char));
  amqp_channel_close(my_session->conn,my_session->channel,AMQP_REPLY_SUCCESS);
  amqp_connection_close(my_session->conn,AMQP_REPLY_SUCCESS);
  strcpy(msg,"Session closed successfully.\n");
  write(my_instance->logfd,msg,strlen(msg));
  my_instance->sessions--;
  if(my_instance->sessions < 1){
   close(my_instance->logfd);
  }
  free(msg);
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
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
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
  char		*ptr;
  int		length;
  
  amqp_basic_properties_t prop;
  prop._flags = AMQP_BASIC_CONTENT_TYPE_FLAG|AMQP_BASIC_DELIVERY_MODE_FLAG;
  prop.content_type = amqp_cstring_bytes("text/plain");
  prop.delivery_mode = 2;
    if (modutil_extract_SQL(queue, &ptr, &length))
    {
     amqp_basic_publish(my_session->conn,my_session->channel,
			amqp_cstring_bytes(""),
			amqp_cstring_bytes("testqueue"),
			0,0,&prop,amqp_cstring_bytes(ptr));
    write(my_instance->logfd, ptr, length);
    write(my_instance->logfd, "\n", 1);
    }
  
  /* Pass the query downstream */
  return my_session->down.routeQuery(my_session->down.instance,
				     my_session->down.session, queue);
}

/**
 * Diagnostics routine
 *
 * If fsession is NULL then print diagnostics on the filter
 * instance as a whole, otherwise print diagnostics for the
 * particular session.
 *
 * @param	instance	The filter instance
 * @param	fsession	Filter session, may be NULL
 * @param	dcb		The DCB for diagnostic output
 */
static	void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
  MQ_INSTANCE	*my_session = (MQ_INSTANCE *)instance;

  if (my_session)
    {
      dcb_printf(dcb, "\t\tConnecting to %s:%d as %s.\n",
		 my_session->hostname,my_session->port,my_session->username);
    }
}

/*
 * This file is distributed as part of MaxScale by MariaDB Corporation.  It is free
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
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 * @file fwfilter.c
 * Firewall Filter
 *
 * A filter that acts as a firewall, blocking queries that do not meet the set requirements.
 */
#include <stdio.h>
#include <fcntl.h>
#include <filter.h>
#include <string.h>
#include <atomic.h>
#include <modutil.h>
#include <log_manager.h>
#include <query_classifier.h>
#include <spinlock.h>
#include <session.h>
#include <plugin.h>
#include <stdint.h>
#include <skygw_types.h>


MODULE_INFO 	info = {
	MODULE_API_FILTER,
	MODULE_ALPHA_RELEASE,
	FILTER_VERSION,
	"Firewall Filter"
};

static int sess_num;

/**
 * Utility function to check if a string contains a valid IP address.
 * The string handled as a null-terminated string.
 * @param str String to parse
 * @return True if the string contains a valid IP address.
 */
bool valid_ip(char* str)
{
    int octval = 0;
	bool valid = true;
	char cmpbuff[32];
	char *source = str,*dest = cmpbuff,*end = strchr(str,'\0');
    
	while(source < end && (int)(dest - cmpbuff) < 32 && valid){
		switch(*source){

		case '.':
		case '/':
		case '\0':
			/**End of IP, string or octet*/
			*(dest++) = '\0';
			octval = atoi(cmpbuff);
			dest = cmpbuff;
			valid = octval < 256 && octval > -1 ? true: false;
			if(*source == '/' || *source == '\0'){
				return valid;
			}else{
				source++;
			}
			break;

		default:
			/**In the IP octet, copy to buffer*/
			*(dest++) = *(source++);
			break;
		}
	}	
	
	return valid;
}

/**
 * Get one octet of IP
 */
int get_octet(char* str)
{
    int octval = 0,retval = -1;
	bool valid = false;
	char cmpbuff[32];
	char *source = str,*dest = cmpbuff,*end = strchr(str,'\0');
    
	if(end == NULL){
		return retval;
	}

	while(source < end && (int)(dest - cmpbuff) < 32 && !valid){
		switch(*source){

			/**End of IP or string or the octet is done*/
		case '.':
		case '/':
		case '\0':
			
			*(dest++) = '\0';
			source++;
			octval = atoi(cmpbuff);
			dest = cmpbuff;
			valid = octval < 256 && octval > -1 ? true: false;
			if(valid)
				{
					retval = octval;
				}
			
			break;

		default:
			/**In the IP octet, copy to buffer*/
			*(dest++) = *(source++);
			break;
		}
	}	
	
	return retval;

}

/**
 *Convert string with IP address to an unsigned 32-bit integer
 * @param str String to convert
 * @return Value of the IP converted to an unsigned 32-bit integer or zero in case of an error.
 */
uint32_t strtoip(char* str)
{
	uint32_t ip = 0;
	char* tok = str;
	if(!valid_ip(str)){
		return 0;
	}
	
	ip = get_octet(tok);
	tok = strchr(tok,'.') + 1;
	ip |= (get_octet(tok))<< 8;
	tok = strchr(tok,'.') + 1;
	ip |= (get_octet(tok))<< 16;
	tok = strchr(tok,'.') + 1;
	ip |= (get_octet(tok))<< 24;
	
	return ip;
}

/**
 *Convert string with a subnet mask to an unsigned 32-bit integer
 */
uint32_t strtosubmask(char* str)
{
	uint32_t mask = 0;
	char *ptr;
	
	if(!valid_ip(str) || 
	   (ptr = strchr(str,'/')) == NULL ||
	   !valid_ip(++ptr))
		{
			return mask;
		}
	
	mask = strtoip(ptr+1);
	return mask;
}

static char *version_str = "V1.0.0";

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
 * The Firewall filter instance.
 */

typedef struct {

} FW_INSTANCE;

/**
 * The session structure for Firewall filter.
 */
typedef struct {
	DOWNSTREAM	down;
	UPSTREAM	up;
	SESSION*	session;
	bool		blocked;
} FW_SESSION;

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
	FW_INSTANCE	*my_instance;
  
	if ((my_instance = calloc(1, sizeof(FW_INSTANCE))) == NULL){
		return NULL;
	}
	sess_num = 0;
	return (FILTER *)my_instance;
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
	FW_SESSION	*my_session;

	if ((my_session = calloc(1, sizeof(FW_SESSION))) == NULL){
		return NULL;
	}
	my_session->session = session;
	my_session->blocked = false;
	return my_session;
}



/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static	void 	
closeSession(FILTER *instance, void *session)
{
	//FW_SESSION	*my_session = (FW_SESSION *)session;
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
	FW_SESSION	*my_session = (FW_SESSION *)session;
	free(my_session);
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
	FW_SESSION	*my_session = (FW_SESSION *)session;
	my_session->down = *downstream;
}

static	void	setUpstream(FILTER *instance, void *session, UPSTREAM *upstream)
{
	FW_SESSION	*my_session = (FW_SESSION *)session;
	my_session->up = *upstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static	int	
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
	FW_SESSION	*my_session = (FW_SESSION *)session;
	FW_INSTANCE	*my_instance = (FW_INSTANCE *)instance;
	bool accept = true;
	char *where,*query;
	int len;
    DCB* dcb = my_session->session->client;
	
	if(modutil_is_SQL(queue)){
		
		modutil_extract_SQL(queue, &query, &len);

		where = skygw_get_where_clause(queue);
		if((where && strchr(where,'*') != NULL) ||
		   (skygw_is_real_query(queue) && memchr(query,'*',len) != NULL)){
			accept = false;
		    skygw_log_write(LOGFILE_TRACE, "where clause with '*': %s", where);
		}

		free(where);

	}
	
	if(!accept){
		
		/**
		 * Convert the query to a fake COM_QUERY with no content
		 * to block the query and trigger an error package from the backend.
		 */
	    
		my_session->blocked = true;
		
		*((unsigned char*)queue->start) = 0x02;
		*((unsigned char*)queue->start + 1) = 0x00;
		*((unsigned char*)queue->start + 2) = 0x00;
 		*((unsigned char*)queue->start + 3) = 0x00;
		*((unsigned char*)queue->start + 4) = 0x03;
		*((unsigned char*)queue->start + 5) = ';';

	}

	return my_session->down.routeQuery(my_session->down.instance,
									   my_session->down.session, queue);
}

/**
 * Checks if the packet contains an empty query error
 * and if the session blocked the last query
 * @param buf Buffer to inspect
 * @param session Filter session object
 * @return true if the error is the right one and the previous query was blocked
 */
bool is_dummy(GWBUF* buf,FW_SESSION* session)
{
	return(*((unsigned char*)buf->start + 4) == 0xff && 
		   *((unsigned char*)buf->start + 5) == 0x29 &&
		   *((unsigned char*)buf->start + 6) == 0x04 && 
		   session->blocked);
}

/**
 * Generates a dummy error packet for the client.
 * @return The dummy packet or NULL if an error occurred
 */
GWBUF* gen_dummy_error()
{
	GWBUF* buf;
	const char* errmsg = "Access denied.";
	unsigned int errlen = strlen(errmsg),
		pktlen = errlen + 9;
	buf = gwbuf_alloc(13 + errlen);
	if(buf){
		strcpy(buf->start + 7,"#HY000");
		memcpy(buf->start + 13,errmsg,errlen);
		*((unsigned char*)buf->start + 0) = pktlen;
		*((unsigned char*)buf->start + 1) = pktlen >> 8;
		*((unsigned char*)buf->start + 2) = pktlen >> 16;
		*((unsigned char*)buf->start + 3) = 0x01;
		*((unsigned char*)buf->start + 4) = 0xff;
		*((unsigned char*)buf->start + 5) = (unsigned char)1141;
		*((unsigned char*)buf->start + 6) = (unsigned char)(1141 >> 8);
	}
	return buf;
}

/**
 * The clientReply entry point. This is passed the response buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the upstream component
 * (filter or router) in the filter chain.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param reply		The response data
 */
static int clientReply(FILTER* instance, void *session, GWBUF *reply)
{
	FW_SESSION		*my_session = (FW_SESSION *)session;
	FW_INSTANCE		*my_instance = (FW_INSTANCE *)instance;
	GWBUF* forward = reply;

	if(is_dummy(reply,my_session)){
		gwbuf_free(reply);
	    forward = gen_dummy_error();
		my_session->blocked = false;
	}

	return my_session->up.clientReply(my_session->up.instance,
									  my_session->up.session, forward);
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
	FW_INSTANCE	*my_instance = (FW_INSTANCE *)instance;

	if (my_instance)
		{
			dcb_printf(dcb, "\t\tFirewall Filter\n");
		}
}
	

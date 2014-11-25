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
 * @file luafilter.c
 * Lua Filter
 *
 * A filter that calls a set of Lua script functions.
 *
 * This filter calls the createInstance, newSession, closeSession, routeQuery and clientReply functions in the Lua script.
 * Out of these functions the newSession, closeSession, routeQuery and clientReply functions are 
 * In addition to being called with the service-specific global Lua state created during createInstance, the following functions
 * are called with a Lua state bound to the client session: newSession, closeSession, routeQuery and clientReply
 */

#include <my_config.h>
#include <skygw_types.h>
#include <spinlock.h>
#include <plugin.h>
#include <skygw_debug.h>
#include <log_manager.h>
extern "C" 
{
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <filter.h>
#include <session.h>
#include <modutil.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

static const char* filter_name = "Lua Filter";

MODULE_INFO 	info = {
	MODULE_API_FILTER,
	MODULE_ALPHA_RELEASE,
	FILTER_VERSION,
	(char*)filter_name
};

static const char *version_str = "V1.0.0";
/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return (char*)version_str;
}


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
static int id_pool = 0;

static int id_gen(lua_State* state)
{
	lua_pushinteger(state,id_pool++);
	return 1;
}

/**
 * The Lua filter instance.
 */
typedef struct {
	lua_State* global_state;
	char* global_script;
	char* session_script;
} LUA_INSTANCE;

/**
 * The session structure for Firewall filter.
 */
typedef struct {
	SESSION*	session;
	lua_State* state;
	DOWNSTREAM	down;
	UPSTREAM	up;
} LUA_SESSION;



/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
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
	LUA_INSTANCE	*my_instance;
	int i;
	if ((my_instance = (LUA_INSTANCE*)calloc(1, sizeof(LUA_INSTANCE))) == NULL){
		return NULL;
	}

	for(i = 0;params[i];i++){
		
		if(strcmp(params[i]->name, "global_script") == 0){
			my_instance->global_script = strdup(params[i]->value);
		}else if(strcmp(params[i]->name, "session_script") == 0){
			my_instance->session_script =  strdup(params[i]->value);
		}
	}

	if(my_instance->global_script)
		{
			my_instance->global_state = luaL_newstate();
			if(my_instance->global_state == NULL){
				skygw_log_write(LOGFILE_ERROR, "Unable to initialize new Lua state.");
				return NULL;
			}

			luaL_openlibs(my_instance->global_state);

			if(luaL_dofile(my_instance->global_state,my_instance->global_script)){
				skygw_log_write(LOGFILE_ERROR, "luafilter: Failed to execute global script at '%s'.",my_instance->global_script);
				free(my_instance->global_script);
				free(my_instance->session_script);
				free(my_instance);
				return NULL;
			}

			if(my_instance->global_state){
				lua_getglobal(my_instance->global_state,"createInstance");
				if(lua_pcall(my_instance->global_state,0,0,0)){
					skygw_log_write(LOGFILE_ERROR, "luafilter: Failed to get global variable 'createInstance':  %s.",
									lua_tostring(my_instance->global_state,-1));
				}
			}
		}

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
	LUA_SESSION	*my_session;
	LUA_INSTANCE *my_instance = (LUA_INSTANCE*)instance;

	if ((my_session = (LUA_SESSION*)calloc(1, sizeof(LUA_SESSION))) == NULL){
		return NULL;
	}
	my_session->session = session;
	my_session->state = luaL_newstate();
	luaL_openlibs(my_session->state);
	if(luaL_dofile(my_session->state,my_instance->session_script)){
		skygw_log_write(LOGFILE_ERROR, "luafilter: Failed to execute session script at '%s': %s.",
						my_instance->session_script,
						lua_tostring(my_session->state,-1));
		return NULL;
	}
	
	lua_pushcfunction(my_session->state,id_gen);
	lua_setglobal(my_session->state,"id_gen");

	lua_getglobal(my_session->state,"newSession");
	if( lua_pcall(my_session->state,0,0,0)){
		skygw_log_write(LOGFILE_ERROR, "luafilter: Failed to get global variable 'newSession': '%s'.",
						lua_tostring(my_session->state,-1));
	}

	if(my_instance->global_state){
		lua_getglobal(my_instance->global_state,"newSession");
		if( lua_pcall(my_instance->global_state,0,0,0)){
			skygw_log_write(LOGFILE_ERROR, "luafilter: Failed to get global variable 'newSession': '%s'.",
							lua_tostring(my_instance->global_state,-1));
		}
	}
	

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
	LUA_SESSION	*my_session = (LUA_SESSION *)session;
	LUA_INSTANCE *my_instance = (LUA_INSTANCE*)instance;

	lua_getglobal(my_session->state,"closeSession");
	if(lua_pcall(my_session->state,0,0,0)){
		skygw_log_write(LOGFILE_ERROR, "luafilter: Failed to get global variable 'closeSession': '%s'.",
						lua_tostring(my_session->state,-1));
	}

	lua_close(my_session->state);

	if(my_instance->global_state){

		lua_getglobal(my_instance->global_state,"closeSession");
		if(lua_pcall(my_instance->global_state,0,0,0)){
			skygw_log_write(LOGFILE_ERROR, "luafilter: Failed to get global variable 'closeSession': '%s'.",
							lua_tostring(my_instance->global_state,-1));
		}
	}

	
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
	LUA_SESSION	*my_session = (LUA_SESSION *)session;
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
	LUA_SESSION	*my_session = (LUA_SESSION *)session;
	my_session->down = *downstream;
}

static	void	setUpstream(FILTER *instance, void *session, UPSTREAM *upstream)
{
  LUA_SESSION	*my_session = (LUA_SESSION *)session;
  my_session->up = *upstream;
}


static	int	clientReply(FILTER *instance, void *session, GWBUF *queue)
{
	LUA_SESSION	*my_session = (LUA_SESSION *)session;
	LUA_INSTANCE	*my_instance = (LUA_INSTANCE *)instance;
	char *fullquery;
	int qsize;
	
	qsize = (unsigned char*)queue->end - (((unsigned char*)queue->start) + 5);
	fullquery = (char*)calloc(qsize + 1,sizeof(char));
	memcpy(fullquery,(((unsigned char*)queue->start) + 5),qsize);

	if(my_session->state){
		lua_getglobal(my_session->state,"clientReply");
		lua_pushlstring(my_session->state,fullquery,qsize + 1);
		if(lua_pcall(my_session->state,1,0,0)){
			skygw_log_write(LOGFILE_ERROR, "luafilter: Session scope call to 'clientReply' failed: '%s'.",
							lua_tostring(my_session->state,-1));		
		}
	}
	if(my_instance->global_state){
		lua_getglobal(my_instance->global_state,"clientReply");
		lua_pushlstring(my_instance->global_state,fullquery,strlen(fullquery));
		if(lua_pcall(my_instance->global_state,1,0,0)){
			skygw_log_write(LOGFILE_ERROR, "luafilter: Global scope call to 'clientReply' failed: '%s'.",
							lua_tostring(my_session->state,-1));		
		}
	}
	return my_session->up.clientReply(my_session->up.instance,
									  my_session->up.session, queue);
}

GWBUF* gen_comquery_packet(char* query)
{
	unsigned int plen = strlen(query) + 1;
	GWBUF* buffer = gwbuf_alloc(plen + 5);
	if(buffer){
	    *((unsigned char*)buffer->start + 0) = plen;
		*((unsigned char*)buffer->start + 1) = plen >> 8;
		*((unsigned char*)buffer->start + 2) = plen >> 16;
		*((unsigned char*)buffer->start + 3) = 0;
		*((unsigned char*)buffer->start + 4) = 0x03;
		strncpy((char*)buffer->start + 5,query,plen);
		buffer->gwbuf_type = GWBUF_TYPE_MYSQL;
	}
	return buffer;
}
	
/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * The Luafilter calls the session specific and global Lua state object's routeQuery functions.
 * The query is passed as a string parameter to the routeQuery Lua function and the return values of the session specific function,
 * if any were returned, are interpreted. If the first value is a boolean, it is interpreted as a decision whether to pass the query onwards or
 * to send an error packet to the client. If it is a string, the current query is replaced with the return value. If no value is returned
 * or nil is returned, the query is passed on normally downstream to the next filter or router in the chain.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static	int	
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
	LUA_SESSION	*my_session = (LUA_SESSION *)session;
	LUA_INSTANCE	*my_instance = (LUA_INSTANCE *)instance;
	DCB* dcb = my_session->session->client;
	char *fullquery = NULL,*ptr;
	int qlen;
	bool route = true;
	GWBUF* forward = queue;

	if(modutil_is_SQL(queue)){

		modutil_extract_SQL(queue, &ptr, &qlen);
		fullquery = (char*)malloc((qlen + 1) * sizeof(char));
		memcpy(fullquery,ptr,qlen);
		memset(fullquery + qlen,0,1);

		if(my_session->state){
			lua_getglobal(my_session->state,"routeQuery");
			lua_pushlstring(my_session->state,fullquery,strlen(fullquery));
			if(lua_pcall(my_session->state,1,1,0)){
				skygw_log_write(LOGFILE_ERROR, "luafilter: Session scope call to 'routeQuery' failed: '%s'.",
								lua_tostring(my_session->state,-1));		
			}

			if(lua_gettop(my_session->state)){

				if(lua_isstring(my_session->state,-1)){

					free(fullquery);
					fullquery = strdup(lua_tostring(my_session->state,-1));
					forward = gen_comquery_packet(fullquery);

				}else if(lua_isboolean(my_session->state,-1)){

					route = lua_toboolean(my_session->state,-1);

				}
			}
			
		}

		if(my_instance->global_state){
			lua_getglobal(my_instance->global_state,"routeQuery");
			lua_pushlstring(my_instance->global_state,fullquery,strlen(fullquery));
			if(lua_pcall(my_instance->global_state,1,0,0)){
				skygw_log_write(LOGFILE_ERROR, "luafilter: Global scope call to 'routeQuery' failed: '%s'.",
								lua_tostring(my_session->state,-1));		
			}
		}

		free(fullquery);
	}

	if(!route){

		free(queue);
		forward = modutil_create_mysql_err_msg(1,0,1045,"28000","Access denied.");	
		forward->gwbuf_type = GWBUF_TYPE_MYSQL;
		return dcb->func.write(dcb,forward);
	}

	return my_session->down.routeQuery(my_session->down.instance,
									   my_session->down.session, forward);
	
}

/**
 * Diagnostics routine
 *
 * @param	instance	The filter instance
 * @param	fsession	Filter session, may be NULL
 * @param	dcb		The DCB for diagnostic output
 */
static	void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
	LUA_INSTANCE	*my_instance = (LUA_INSTANCE *)instance;

	if (my_instance)
		{
			dcb_printf(dcb, "\t\tLua Filter\nglobal script: %s\nsession script: %s\n",my_instance->global_script,my_instance->session_script);
		}
}
}	

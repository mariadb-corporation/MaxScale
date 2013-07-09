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
#include <stdio.h>
#include <router.h>

# include <stdlib.h>
# include <mysql.h>
# include <skygw_utils.h>
# include <log_manager.h>
# include <query_classifier.h>

/**
 * @file router.c	The entry points for the read/write query splitting
 * router module.
 *
 * This file contains the entry points that comprise the API to the read write
 * query splitting router.
 *
 */
static char *version_str = "V1.0.0";

static	ROUTER	*createInstance(SERVICE *service, char **options);
static	void	*newSession(ROUTER *instance, SESSION *session);
static	void 	closeSession(ROUTER *instance, void *session);
static	int	    routeQuery(ROUTER *instance, void *session, GWBUF *queue);
static	void	diagnostic(ROUTER *instance, DCB *dcb);

static ROUTER_OBJECT MyObject = { createInstance, newSession, closeSession, routeQuery, diagnostic };


#if defined(SS_DEBUG)
static void vilhos_test_for_query_classifier(void)
{
        bool failp;
        MYSQL* mysql = NULL;

        /**
         * Init libmysqld.
         */
        ss_dassert(mysql_thread_safe());
        mysql_thread_init();

        char* str = (char *)calloc(1,
                                   sizeof("Query type is ")+
                                   sizeof("QUERY_TYPE_SESSION_WRITE"));
        /**
         * Call query classifier.
         */
        sprintf(str,
                "Query type is %s\n",
                STRQTYPE(
                        skygw_query_classifier_get_type(
                                "SELECT user from mysql.user", 0)));
        /**
         * generate some log
         */
        skygw_log_write(NULL, LOGFILE_MESSAGE,str);
        
        mysql_close(mysql);
        mysql_thread_end();
        
return_without_server:
        ss_dfprintf(stderr, "\n<< testmain\n");
        fflush(stderr);
}
#endif /* SS_DEBUG */

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
// rename_libfuncs();
#if defined(SS_DEBUG)
    vilhos_test_for_query_classifier();
#endif
	fprintf(stderr, "Initialse read/writer splitting query router module.\n");
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
ROUTER_OBJECT *
GetModuleObject()
{
	fprintf(stderr, "Returing test router module object.\n");
	return &MyObject;
}

/**
 * Create an instance of the router for a particular service
 * within the gateway.
 *
 * The job of ths entry point is to create the service wide data needed
 * for the query router. This is information needed to route queries that
 * is not related to any individual client session, exmaples of data that
 * might be stored in the ROUTER object for a particular query router are
 * connections counts, last used connection etc so that balancing may
 * take place.
 * 
 * @param service	The service this router is being create for
 * @param options	The options for this query router
 *
 * @return The instance data for this new instance
 */
static	ROUTER	*
createInstance(SERVICE *service, char **options)
{
	return NULL;
}

/**
 * Associate a new session with this instance of the router.
 *
 * The session is used to store all the data required for a particular
 * client connection.
 *
 * @param instance	The router instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(ROUTER *instance, SESSION *session)
{
	return NULL;
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance	The router instance data
 * @param session	The session being closed
 */
static	void 	
closeSession(ROUTER *instance, void *session)
{
}


/**
 * The main routing entry, this is called with every packet that is
 * received and has to be forwarded to the backend database.
 *
 * The routeQuery will make the routing decision based on the contents
 * of the instance, session and the query itself in the queue. The
 * data in the queue may not represent a complete query, it represents
 * the data that has been received. The query router itself is responsible
 * for buffering the partial query, a later call to the query router will
 * contain the remainder, or part thereof of the query.
 *
 * @param instance	The query router instance
 * @param session	The session assoicated with the client
 * @param queue		Gateway buffer queue with the packets received
 *
 * @return The number of queries forwarded
 */
static	int	
routeQuery(ROUTER *instance, void *session, GWBUF *queue)
{
	return 0;
}

/**
 * Diagnostics routine
 *
 * Print query router statistics to the DCB passed in
 *
 * @param	instance	The router instance
 * @param	dcb		The DCB for diagnostic output
 */
static	void
diagnostic(ROUTER *instance, DCB *dcb)
{
}

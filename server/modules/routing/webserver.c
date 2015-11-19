/*
 * This file is distributed as part of MaxScale.  It is free
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
#include <stdio.h>
#include <router.h>
#include <modinfo.h>
#include <server.h>
#include <service.h>
#include <session.h>
#include <monitor.h>
#include <string.h>

/**
 * The instance structure for this router.
 */
typedef struct {
	SERVICE		*service;
} WEB_INSTANCE;

/**
 * The session structure for this router.
 */
typedef struct {
	SESSION		*session;
} WEB_SESSION;

static char *version_str = "V1.0.0";

MODULE_INFO 	info = {
	MODULE_API_ROUTER,
	MODULE_IN_DEVELOPMENT,
	ROUTER_VERSION,
	"A test router - not for use in real systems"
};

static	ROUTER	*createInstance(SERVICE *service, char **options);
static	void	*newSession(ROUTER *instance, SESSION *session);
static	void 	closeSession(ROUTER *instance, void *session);
static	void 	freeSession(ROUTER *instance, void *session);
static	int	routeQuery(ROUTER *instance, void *session, GWBUF *queue);
static	void	diagnostic(ROUTER *instance, DCB *dcb);
static  uint8_t getCapabilities (ROUTER* inst, void* router_session);


static ROUTER_OBJECT MyObject = {
    createInstance,
    newSession,
    closeSession,
    freeSession,
    routeQuery,
    diagnostic,
    NULL,
    NULL,
    getCapabilities
};


static void	send_index(WEB_SESSION *);
static void	send_css(WEB_SESSION *);
static void	send_menu(WEB_SESSION *);
static void	send_blank(WEB_SESSION *);
static void	send_title(WEB_SESSION *);
static void	send_frame1(WEB_SESSION *);
static void	send_services(WEB_SESSION *);
static void	send_sessions(WEB_SESSION *);
static void	send_servers(WEB_SESSION *);
static void	send_monitors(WEB_SESSION *);
static void	respond_error(WEB_SESSION *, int, char *);

/**
 * A map of URL to function that implements the URL
 */
static struct {
	char		*page;		/* URL */
	void (*fcn)(WEB_SESSION *);	/* Function to call */
} pages[] = {
	{ "index.html", send_index },
	{ "services.html", send_services },
	{ "menu.html", send_menu },
	{ "sessions.html", send_sessions },
	{ "blank.html", send_blank },
	{ "title.html", send_title },
	{ "frame1.html", send_frame1 },
	{ "servers.html", send_servers },
	{ "monitors.html", send_monitors },
	{ "styles.css", send_css },
	{ NULL, NULL }
};

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
ROUTER_OBJECT *
GetModuleObject()
{
	return &MyObject;
}

/**
 * Create an instance of the router for a particular service
 * within the gateway.
 * 
 * @param service	The service this router is being create for
 * @param options	The options for this query router
 *
 * @return The instance data for this new instance
 */
static	ROUTER	*
createInstance(SERVICE *service, char **options)
{
WEB_INSTANCE	*inst;

	if ((inst = (WEB_INSTANCE *)malloc(sizeof(WEB_INSTANCE))) == NULL)
		return NULL;

	inst->service = service;
	return (ROUTER *)inst;
}

/**
 * Associate a new session with this instance of the router.
 *
 * @param instance	The router instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(ROUTER *instance, SESSION *session)
{
WEB_SESSION	*wsession;

	if ((wsession = (WEB_SESSION *)malloc(sizeof(WEB_SESSION))) == NULL)
		return NULL;

	wsession->session = session;
	return wsession;
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
	free(session);
}

static void freeSession(
        ROUTER* router_instance,
        void*   router_client_session)
{
        return;
}

static	int	
routeQuery(ROUTER *instance, void *session, GWBUF *queue)
{
WEB_SESSION	*wsession = (WEB_SESSION *)session;
char		*ptr;
int		i, found = 0;
char		*url;

	if ((url = gwbuf_get_property(queue, "URL")) == NULL)
	{
		respond_error(wsession, 404, "No URL available");
	}

	ptr = strrchr(url, '/');
	if (ptr)
		ptr++;
	else
		ptr = url;
	for (i = 0; pages[i].page; i++)
	{
		if (!strcmp(ptr, pages[i].page))
		{
			(pages[i].fcn)(wsession);
			found = 1;
		}
	}
	if (!found)
		respond_error(wsession, 404, "Unrecognised URL received");
	gwbuf_free(queue);
	return 0;
}

/**
 * Diagnostics routine
 *
 * @param	instance	The router instance
 * @param	dcb		The DCB for diagnostic output
 */
static	void
diagnostic(ROUTER *instance, DCB *dcb)
{
}

/**
 * Return the router capabilities bitmask
 *
 * @param inst			The router instance
 * @param router_session	The router session
 * @return Router capabilities bitmask
 */
static uint8_t
getCapabilities(ROUTER *inst, void *router_session)
{
        return 0;
}

/**
 * The HTML of the index page.
 */
static char *index_page =
"<HTML><HEAD>"
"<LINK REL=\"stylesheet\" type=\"text/css\" href=\"styles.css\">"
"<TITLE>MaxScale</TITLE>"
"</HEAD>"
"<FRAMESET ROWS=\"60,*\">"
"<FRAME SRC=\"title.html\">"
"<FRAME SRC=\"frame1.html\">"
"</FRAMESET>"
"</HTML>";

/**
 * The HTML of the title page
 */
static char *title_page =
"<HTML><HEAD>"
"<LINK REL=\"stylesheet\" type=\"text/css\" href=\"styles.css\">"
"<TITLE>MaxScale</TITLE>"
"</HEAD><BODY>"
"<H1>MaxScale - Status View</H1>"
"</BODY></HTML>";

/**
 * HTML of the main frames, those below the title frame
 */
static char *frame1_page =
"<HTML>"
"<FRAMESET COLS=\"20%,80%\">"
"<FRAME SRC=\"menu.html\">"
"<FRAME SRC=\"blank.html\" NAME=\"darea\">"
"</FRAMESET>"
"</HTML>";

/**
 * The menu page HTML
 */
static char *menu_page =
"<HTML><HEAD>"
"<LINK REL=\"stylesheet\" type=\"text/css\" href=\"styles.css\">"
"</HEAD><BODY>"
"<H2>Options</H2><P>"
"<UL><LI><A HREF=\"monitors.html\" target=\"darea\">Monitors</A>"
"<LI><A HREF=\"services.html\" target=\"darea\">Services</A>"
"<LI><A HREF=\"servers.html\" target=\"darea\">Servers</A>"
"<LI><A HREF=\"sessions.html\" target=\"darea\">Sessions</A>"
"</UL></BODY></HTML>";

/**
 * A blank page, contents of the display area when we first connect
 */
static char *blank_page = "<HTML><BODY>&nbsp;</BODY></HTML>";

/**
 * The CSS used for every "page"
 */
static char *css =
"table, td, th { border: 1px solid blue; }\n"
"th { background-color: blue; color: white; padding: 5px }\n"
"td { padding: 5px; }\n"
"table { border-collapse: collapse; }\n"
"a:link { color: #0000FF; }\n"
"a:visted { color: #0000FF; }\n"
"a:hover { color: #FF0000; }\n"
"a:active { color: #0000FF; }\n"
"h1 { color: blue; font-family: serif }\n"
"h2 { color: blue; font-family: serif }\n"
"p { font-family: serif }\n"
"li { font-family: serif }\n";

/**
 * Send the standard HTTP headers for an HTML file
 */
static void
send_html_header(DCB *dcb)
{
char date[64] = "";
const char *fmt = "%a, %d %b %Y %H:%M:%S GMT";

	time_t httpd_current_time = time(NULL);
        struct tm tm;
        char buffer[32]; // asctime_r documentation requires 26

        localtime_r(&http_current_time, &tm);
        asctime_r(&tm, buffer);

	strftime(date, sizeof(date), fmt, buffer);

	dcb_printf(dcb, "HTTP/1.1 200 OK\r\nDate: %s\r\nServer: %s\r\nConnection: close\r\nContent-Type: text/html\r\n", date, "MaxScale");

	dcb_printf(dcb, "\r\n");
}

/**
 * Send a static HTML page
 *
 * @param dcb		The DCB of the connection to the browser
 * @param html		The HTML to send
 */
static void
send_static_html(DCB *dcb, char *html)
{
	dcb_printf(dcb, html);
}

/**
 * Send the index page
 *
 * @param session	The router session
 */
static void
send_index(WEB_SESSION	*session)
{
DCB	*dcb = session->session->client;

	send_html_header(dcb);
	send_static_html(dcb, index_page);
	dcb_close(dcb);
}

/**
 * Send the CSS
 *
 * @param session	The router session
 */
static void
send_css(WEB_SESSION	*session)
{
DCB	*dcb = session->session->client;

	send_html_header(dcb);
	send_static_html(dcb, css);
	dcb_close(dcb);
}

/**
 * Send the title page
 *
 * @param session	The router session
 */
static void
send_title(WEB_SESSION	*session)
{
DCB	*dcb = session->session->client;

	send_html_header(dcb);
	send_static_html(dcb, title_page);
	dcb_close(dcb);
}

/**
 * Send the frame1 page
 *
 * @param session	The router session
 */
static void
send_frame1(WEB_SESSION	*session)
{
DCB	*dcb = session->session->client;

	send_html_header(dcb);
	send_static_html(dcb, frame1_page);
	dcb_close(dcb);
}

/**
 * Send the menu page
 *
 * @param session	The router session
 */
static void
send_menu(WEB_SESSION	*session)
{
DCB	*dcb = session->session->client;

	send_html_header(dcb);
	send_static_html(dcb, menu_page);
	dcb_close(dcb);
}

/**
 * Send a blank page
 *
 * @param session	The router session
 */
static void
send_blank(WEB_SESSION	*session)
{
DCB	*dcb = session->session->client;

	send_html_header(dcb);
	send_static_html(dcb, blank_page);
	dcb_close(dcb);
}

/**
 * Write a table row for a service. This is called using the service
 * iterator function
 *
 * @param service	The service to display
 * @param dcb		The DCB to print the HTML to
 */
static void
service_row(SERVICE *service, DCB *dcb)
{
	dcb_printf(dcb, "<TR><TD>%s</TD><TD>%s</TD><TD>%d</TD><TD>%d</TD></TR>\n",
		service->name, service->routerModule,
		service->stats.n_current, service->stats.n_sessions);
}

/**
 * Send the services page. This produces a table by means of the 
 * serviceIterate call.
 *
 * @param session	The router session
 */
static void
send_services(WEB_SESSION *session)
{
DCB	*dcb = session->session->client;

	send_html_header(dcb);
	dcb_printf(dcb, "<HTML><HEAD>");
	dcb_printf(dcb, "<LINK REL=\"stylesheet\" type=\"text/css\" href=\"styles.css\">");
	dcb_printf(dcb, "<BODY><H2>Services</H2><P>");
	dcb_printf(dcb, "<TABLE><TR><TH>Name</TH><TH>Router</TH><TH>");
	dcb_printf(dcb, "Current Sessions</TH><TH>Total Sessions</TH></TR>\n");
	serviceIterate(service_row, dcb);
	dcb_printf(dcb, "</TABLE></BODY></HTML>\n");
	dcb_close(dcb);
}

/**
 * Write a session row for a session. this is called using the session
 * iterator function
 *
 * @param session	The session to display
 * @param dcb		The DCB to send the HTML to
 */
static void
session_row(SESSION *session, DCB *dcb)
{
	dcb_printf(dcb, "<TR><TD>%-16p</TD><TD>%s</TD><TD>%s</TD><TD>%s</TD></TR>\n",
		session, ((session->client && session->client->remote)
			? session->client->remote : ""),
                        (session->service && session->service->name
				? session->service->name : ""),
                        session_state(session->state));
}

/**
 * Send the sessions page. The produces a table of all the current sessions
 * display. It makes use of the sessionIterate call to call the function
 * session_row() with each session.
 *
 * @param session	The router session
 */
static void
send_sessions(WEB_SESSION *session)
{
DCB	*dcb = session->session->client;

	send_html_header(dcb);
	dcb_printf(dcb, "<HTML><HEAD>");
	dcb_printf(dcb, "<LINK REL=\"stylesheet\" type=\"text/css\" href=\"styles.css\">");
	dcb_printf(dcb, "<BODY><H2>Sessions</H2><P>");
	dcb_printf(dcb, "<TABLE><TR><TH>Session</TH><TH>Client</TH><TH>");
	dcb_printf(dcb, "Service</TH><TH>State</TH></TR>\n");
	sessionIterate(session_row, dcb);
	dcb_printf(dcb, "</TABLE></BODY></HTML>\n");
	dcb_close(dcb);
}

/**
 * Display a table row for a particular server. This is called via the
 * serverIterate call in send_servers.
 *
 * @param server	The server to print
 * @param dcb		The DCB to send the HTML to
 */
static void
server_row(SERVER *server, DCB *dcb)
{
	dcb_printf(dcb, "<TR><TD>%s</TD><TD>%s</TD><TD>%d</TD><TD>%s</TD><TD>%d</TD></TR>\n",
		server->unique_name, server->name, server->port,
		server_status(server), server->stats.n_current);
}

/**
 * Send the servers page
 *
 * @param session	The router session
 */
static void
send_servers(WEB_SESSION *session)
{
DCB	*dcb = session->session->client;

	send_html_header(dcb);
	dcb_printf(dcb, "<HTML><HEAD>");
	dcb_printf(dcb, "<LINK REL=\"stylesheet\" type=\"text/css\" href=\"styles.css\">");
	dcb_printf(dcb, "<BODY><H2>Servers</H2><P>");
	dcb_printf(dcb, "<TABLE><TR><TH>Server</TH><TH>Address</TH><TH>");
	dcb_printf(dcb, "Port</TH><TH>State</TH><TH>Connections</TH></TR>\n");
	serverIterate(server_row, dcb);
	dcb_printf(dcb, "</TABLE></BODY></HTML>\n");
	dcb_close(dcb);
}

/**
 * Print a table row for the monitors table
 *
 * @param monitor	The monitor to print
 * @param dcb		The DCB to print to
 */
static void
monitor_row(MONITOR *monitor, DCB *dcb)
{
	dcb_printf(dcb, "<TR><TD>%s</TD><TD>%s</TD></TR>\n",
		monitor->name, monitor->state & MONITOR_STATE_RUNNING
			? "Running" : "Stopped");
}

/**
 * Send the monitors page. This iterates on all the monitors and send
 * the rows via the monitor_monitor.
 *
 * @param session	The router session
 */
static void
send_monitors(WEB_SESSION *session)
{
DCB	*dcb = session->session->client;

	send_html_header(dcb);
	dcb_printf(dcb, "<HTML><HEAD>");
	dcb_printf(dcb, "<LINK REL=\"stylesheet\" type=\"text/css\" href=\"styles.css\">");
	dcb_printf(dcb, "<BODY><H2>Monitors</H2><P>");
	dcb_printf(dcb, "<TABLE><TR><TH>Monitor</TH><TH>State</TH></TR>\n");
	monitorIterate(monitor_row, dcb);
	dcb_printf(dcb, "</TABLE></BODY></HTML>\n");
	dcb_close(dcb);
}

/**
 * Respond with an HTTP error
 *
 * @param session	The router session
 * @param err		The HTTP error code to send
 * @param msg		The message to print
 */
static void
respond_error(WEB_SESSION *session, int err, char *msg)
{
DCB	*dcb = session->session->client;

	dcb_printf(dcb, "HTTP/1.1 %d %s\n", err, msg);
	dcb_printf(dcb, "Content-Type: text/html\n");
	dcb_printf(dcb, "\n");
	dcb_printf(dcb, "<HTML><BODY>\n");
	dcb_printf(dcb, "MaxScale webserver plugin unable to satisfy request.\n");
	dcb_printf(dcb, "<P>Code: %d, %s\n", err, msg);
	dcb_printf(dcb, "</BODY></HTML>");
	dcb_close(dcb);
}

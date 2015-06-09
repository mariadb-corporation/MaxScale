/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2015
 */

#include <monitor_common.h>

monitor_event_t mon_name_to_event(char* tok);

/**
 * Set a pending status bit in the monitor server
 *
 * @param server        The server to update
 * @param bit           The bit to clear for the server
 */
void monitor_set_pending_status(MONITOR_SERVERS *ptr, int bit)
{
	ptr->pending_status |= bit;
}

/**
 * Clear a pending status bit in the monitor server
 *
 * @param server        The server to update
 * @param bit           The bit to clear for the server
 */
void monitor_clear_pending_status(MONITOR_SERVERS *ptr, int bit)
{
	ptr->pending_status &= ~bit;
}


 monitor_event_t mon_get_event_type(MONITOR_SERVERS* node)
{
    unsigned int prev = node->mon_prev_status;

    if((prev & (SERVER_MASTER|SERVER_RUNNING)) == (SERVER_MASTER|SERVER_RUNNING) &&
       SERVER_IS_DOWN(node->server))
    {
	return MASTER_DOWN_EVENT;
    }
    if((prev & (SERVER_RUNNING)) == 0 &&
       SERVER_IS_RUNNING(node->server) && SERVER_IS_MASTER(node->server))
    {
	return MASTER_UP_EVENT;
    }
    if((prev & (SERVER_SLAVE|SERVER_RUNNING)) == (SERVER_SLAVE|SERVER_RUNNING) &&
       SERVER_IS_DOWN(node->server))
    {
	return SLAVE_DOWN_EVENT;
    }
    if((prev & (SERVER_RUNNING)) == 0 &&
       SERVER_IS_RUNNING(node->server) && SERVER_IS_SLAVE(node->server))
    {
	return SLAVE_UP_EVENT;
    }

    /** Galera specific events */
    if((prev & (SERVER_JOINED|SERVER_RUNNING)) == (SERVER_JOINED|SERVER_RUNNING) &&
       SERVER_IS_DOWN(node->server))
    {
	return SYNCED_DOWN_EVENT;
    }
    if((prev & (SERVER_RUNNING)) == 0 &&
       SERVER_IS_RUNNING(node->server) && SERVER_IS_JOINED(node->server))
    {
	return SYNCED_UP_EVENT;
    }

    /** NDB events*/
    if((prev & (SERVER_NDB|SERVER_RUNNING)) == (SERVER_NDB|SERVER_RUNNING) &&
       SERVER_IS_DOWN(node->server))
    {
	return NDB_DOWN_EVENT;
    }
    if((prev & (SERVER_RUNNING)) == 0 &&
       SERVER_IS_RUNNING(node->server) && SERVER_IS_NDB(node->server))
    {
	return NDB_UP_EVENT;
    }

    if((prev & (SERVER_RUNNING)) == SERVER_RUNNING &&
       SERVER_IS_RUNNING(node->server) && SERVER_IS_MASTER(node->server))
    {
	return NEW_MASTER_EVENT;
    }
    if((prev & (SERVER_RUNNING)) == SERVER_RUNNING &&
       SERVER_IS_RUNNING(node->server) && SERVER_IS_SLAVE(node->server))
    {
	return NEW_SLAVE_EVENT;
    }

    /** Status loss events */
    if((prev & (SERVER_RUNNING|SERVER_MASTER)) == (SERVER_RUNNING|SERVER_MASTER) &&
       SERVER_IS_RUNNING(node->server) && !SERVER_IS_MASTER(node->server))
    {
	return LOST_MASTER_EVENT;
    }
    if((prev & (SERVER_RUNNING|SERVER_SLAVE)) == (SERVER_RUNNING|SERVER_SLAVE) &&
       SERVER_IS_RUNNING(node->server) && !SERVER_IS_SLAVE(node->server))
    {
	return LOST_SLAVE_EVENT;
    }
    if((prev & (SERVER_RUNNING|SERVER_JOINED)) == (SERVER_RUNNING|SERVER_JOINED) &&
       SERVER_IS_RUNNING(node->server) && !SERVER_IS_JOINED(node->server))
    {
	return LOST_SYNCED_EVENT;
    }
    if((prev & (SERVER_RUNNING|SERVER_NDB)) == (SERVER_RUNNING|SERVER_NDB) &&
       SERVER_IS_RUNNING(node->server) && !SERVER_IS_NDB(node->server))
    {
	return LOST_NDB_EVENT;
    }


    /** Generic server failure */
    if((prev & SERVER_RUNNING) == 0 &&
       SERVER_IS_RUNNING(node->server))
    {
	return SERVER_UP_EVENT;
    }
    if((prev & SERVER_RUNNING) == SERVER_RUNNING &&
       SERVER_IS_DOWN(node->server))
    {
	return SERVER_DOWN_EVENT;
    }

    /** Something else, most likely a state that does not matter.
     * For example SERVER_DOWN -> SERVER_MASTER|SERVER_DOWN still results in a
     * server state equal to not running.*/
    return UNDEFINED_MONITOR_EVENT;
}

char* mon_get_event_name(MONITOR_SERVERS* node)
{
    switch(mon_get_event_type(node))
    {
case UNDEFINED_MONITOR_EVENT:
	return "undefined";

case MASTER_DOWN_EVENT:
	return "master_down";

case MASTER_UP_EVENT:
	return "master_up";

case SLAVE_DOWN_EVENT:
	return "slave_down";

case SLAVE_UP_EVENT:
	return "slave_up";

case SERVER_DOWN_EVENT:
	return "server_down";

case SERVER_UP_EVENT:
	return "server_up";

case SYNCED_DOWN_EVENT:
	return "synced_down";

case SYNCED_UP_EVENT:
	return "synced_up";

case DONOR_DOWN_EVENT:
	return "donor_down";

case DONOR_UP_EVENT:
	return "donor_up";

case NDB_DOWN_EVENT:
	return "ndb_down";

case NDB_UP_EVENT:
	return "ndb_up";

case LOST_MASTER_EVENT:
	return "lost_master";

case LOST_SLAVE_EVENT:
	return "lost_slave";

case LOST_SYNCED_EVENT:
	return "lost_synced";

case LOST_DONOR_EVENT:
	return "lost_donor";

case LOST_NDB_EVENT:
	return "lost_ndb";

case NEW_MASTER_EVENT:
	return "new_master";

case NEW_SLAVE_EVENT:
	return "new_slave";

case NEW_SYNCED_EVENT:
	return "new_synced";

case NEW_DONOR_EVENT:
	return "new_donor";

    case NEW_NDB_EVENT:
	return "new_ndb";

    default:
	return "MONITOR_EVENT_FAILURE";

    }

    
}

void mon_append_node_names(MONITOR_SERVERS* start,char* str, int len)
{
    MONITOR_SERVERS* ptr = start;
    bool first = true;
    int slen = strlen(str);

    while(ptr && slen < len)
    {
	if(!first)
	{
	    strncat(str,",",len);
	}
	first = false;
	strncat(str,ptr->server->unique_name,len);
	ptr = ptr->next;
	slen = strlen(str);
    }
}

/**
 * Check if current monitored server status has changed
 *
 * @param mon_srv       The monitored server
 * @return              true if status has changed or false
 */
bool mon_status_changed(
        MONITOR_SERVERS* mon_srv)
{
        bool succp;

	/** This is the first time the server was set with a status*/
        if (mon_srv->mon_prev_status == -1)
	    return false;

        if (mon_srv->mon_prev_status != mon_srv->server->status)
        {
                succp = true;
        }
        else
        {
                succp = false;
        }
        return succp;
}

/**
 * Check if current monitored server has a loggable failure status
 *
 * @param mon_srv	The monitored server
 * @return		true if failed status can be logged or false
 */
bool mon_print_fail_status(
        MONITOR_SERVERS* mon_srv)
{
        bool succp;
        int errcount = mon_srv->mon_err_count;

        if (SERVER_IS_DOWN(mon_srv->server) && errcount == 0)
        {
                succp = true;
        }
        else
        {
                succp = false;
        }
        return succp;
}

void monitor_launch_script(MONITOR* mon,MONITOR_SERVERS* ptr, char* script)
{
    char argstr[PATH_MAX + MON_ARG_MAX + 1];
    EXTERNCMD* cmd;

    snprintf(argstr,PATH_MAX + MON_ARG_MAX,
	     "%s --event=%s --initiator=%s --nodelist=",
	     script,
	     mon_get_event_name(ptr),
	     ptr->server->unique_name);

    mon_append_node_names(mon->databases,argstr,PATH_MAX + MON_ARG_MAX + 1);
    if((cmd = externcmd_allocate(argstr)) == NULL)
    {
	skygw_log_write(LE,"Failed to execute script: %s",script);
	return;
    }

    if(externcmd_execute(cmd))
    {
	skygw_log_write(LOGFILE_ERROR,
		 "Error: Failed to execute script "
		"'%s' on server state change event %s.",
		 script,mon_get_event_type(ptr));
    }
    externcmd_free(cmd);
}

/**
 * Parse a string of event names to an array with enabled events.
 * @param events Pointer to an array of boolean values
 * @param count Size of the array
 * @param string String to parse
 * @return 0 on success. 1 when an error has occurred or an unexpected event was
 * found.
 */
int mon_parse_event_string(bool* events, size_t count,char* string)
{
    char *tok,*saved;
    monitor_event_t event;

    tok = strtok_r(string,",| ",&saved);

    if(tok == NULL)
	return -1;

    while(tok)
    {
	event = mon_name_to_event(tok);
	if(event == UNDEFINED_MONITOR_EVENT)
	    return -1;
	events[event] = true;
	tok = strtok_r(NULL,",| ",&saved);
    }

    return 0;
}

monitor_event_t mon_name_to_event(char* tok)
{
    if(!strcasecmp("master_down",tok))
	return MASTER_DOWN_EVENT;
    else if(!strcasecmp("master_up",tok))
	return MASTER_UP_EVENT;
    else if(!strcasecmp("slave_down",tok))
	return SLAVE_DOWN_EVENT;
    else if(!strcasecmp("slave_up",tok))
	return SLAVE_UP_EVENT;
    else if(!strcasecmp("server_down",tok))
	return SERVER_DOWN_EVENT;
    else if(!strcasecmp("server_up",tok))
	return SERVER_UP_EVENT;
    else if(!strcasecmp("synced_down",tok))
	return SYNCED_DOWN_EVENT;
    else if(!strcasecmp("synced_up",tok))
	return SYNCED_UP_EVENT;
    else if(!strcasecmp("donor_down",tok))
	return DONOR_DOWN_EVENT;
    else if(!strcasecmp("donor_up",tok))
	return DONOR_UP_EVENT;
    else if(!strcasecmp("ndb_down",tok))
	return NDB_DOWN_EVENT;
    else if(!strcasecmp("ndb_up",tok))
	return NDB_UP_EVENT;
    else if(!strcasecmp("lost_master",tok))
	return LOST_MASTER_EVENT;
    else if(!strcasecmp("lost_slave",tok))
	return LOST_SLAVE_EVENT;
    else if(!strcasecmp("lost_synced",tok))
	return LOST_SYNCED_EVENT;
    else if(!strcasecmp("lost_donor",tok))
	return LOST_DONOR_EVENT;
    else if(!strcasecmp("lost_ndb",tok))
	return LOST_NDB_EVENT;
    else if(!strcasecmp("new_master",tok))
	return NEW_MASTER_EVENT;
    else if(!strcasecmp("new_slave",tok))
	return NEW_SLAVE_EVENT;
    else if(!strcasecmp("new_synced",tok))
	return NEW_SYNCED_EVENT;
    else if(!strcasecmp("new_donor",tok))
	return NEW_DONOR_EVENT;
    else if(!strcasecmp("new_ndb",tok))
	return NEW_NDB_EVENT;
    else
	return UNDEFINED_MONITOR_EVENT;

    }


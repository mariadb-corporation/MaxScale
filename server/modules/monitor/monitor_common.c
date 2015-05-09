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

char* mon_get_event_type(MONITOR_SERVERS* node)
{
    unsigned int prev = node->mon_prev_status;

    if((prev & (SERVER_MASTER|SERVER_RUNNING)) == (SERVER_MASTER|SERVER_RUNNING) &&
       SERVER_IS_DOWN(node->server))
    {
	return "master_down";
    }
    if((prev & (SERVER_RUNNING)) == 0 &&
       SERVER_IS_RUNNING(node->server) && SERVER_IS_MASTER(node->server))
    {
	return "master_up";
    }
    if((prev & (SERVER_SLAVE|SERVER_RUNNING)) == (SERVER_SLAVE|SERVER_RUNNING) &&
       SERVER_IS_DOWN(node->server))
    {
	return "slave_down";
    }
    if((prev & (SERVER_RUNNING)) == 0 &&
       SERVER_IS_RUNNING(node->server) && SERVER_IS_SLAVE(node->server))
    {
	return "slave_up";
    }
    if((prev & (SERVER_RUNNING)) == SERVER_RUNNING &&
       SERVER_IS_RUNNING(node->server) && SERVER_IS_MASTER(node->server))
    {
	return "new_master";
    }
    if((prev & (SERVER_RUNNING)) == SERVER_RUNNING &&
       SERVER_IS_RUNNING(node->server) && SERVER_IS_SLAVE(node->server))
    {
	return "new_slave";
    }
    if((prev & (SERVER_RUNNING|SERVER_MASTER)) == (SERVER_RUNNING|SERVER_MASTER) &&
       SERVER_IS_RUNNING(node->server) && !SERVER_IS_MASTER(node->server))
    {
	return "lost_master";
    }
    if((prev & (SERVER_RUNNING|SERVER_SLAVE)) == (SERVER_RUNNING|SERVER_SLAVE) &&
       SERVER_IS_RUNNING(node->server) && !SERVER_IS_SLAVE(node->server))
    {
	return "lost_slave";
    }
    if((prev & SERVER_RUNNING) == 0 &&
       SERVER_IS_RUNNING(node->server))
    {
	return "server_up";
    }
    if((prev & SERVER_RUNNING) == SERVER_RUNNING &&
       SERVER_IS_DOWN(node->server))
    {
	return "server_down";
    }
    return "unknown";
}

void mon_append_node_names(MONITOR_SERVERS* start,char* str, int len)
{
    MONITOR_SERVERS* ptr = start;
    bool first = true;

    while(ptr)
    {
	if(!first)
	{
	    strncat(str,",",len);
	}
	first = false;
	strncat(str,ptr->server->unique_name,len);
	ptr = ptr->next;
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

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
 * Copyright MariaDB Corporation Ab 2013-2014
 */

/**
 * @file def_monitor_event.h
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 01-06-2013	Martin Brampton		Initial implementation
 */

ADDITEM( UNDEFINED_MONITOR_EVENT, undefined ),
ADDITEM( MASTER_DOWN_EVENT, master_down ),
ADDITEM( MASTER_UP_EVENT, master_up ),
ADDITEM( SLAVE_DOWN_EVENT, slave_down ),
ADDITEM( SLAVE_UP_EVENT, slave_up ),
ADDITEM( SERVER_DOWN_EVENT, server_down ),
ADDITEM( SERVER_UP_EVENT, server_up ),
ADDITEM( SYNCED_DOWN_EVENT, synced_down ),
ADDITEM( SYNCED_UP_EVENT, synced_up ),
ADDITEM( DONOR_DOWN_EVENT, donor_down ),
ADDITEM( DONOR_UP_EVENT, donor_up ),
ADDITEM( NDB_DOWN_EVENT, ndb_down ),
ADDITEM( NDB_UP_EVENT, ndb_up ),
ADDITEM( LOST_MASTER_EVENT, lost_master ),
ADDITEM( LOST_SLAVE_EVENT, lost_slave ),
ADDITEM( LOST_SYNCED_EVENT, lost_synced ),
ADDITEM( LOST_DONOR_EVENT, lost_donor ),
ADDITEM( LOST_NDB_EVENT, lost_ndb ),
ADDITEM( NEW_MASTER_EVENT, new_master ),
ADDITEM( NEW_SLAVE_EVENT, new_slave ),
ADDITEM( NEW_SYNCED_EVENT, new_synced ),
ADDITEM( NEW_DONOR_EVENT, new_donor ),
ADDITEM( NEW_NDB_EVENT, new_ndb ),

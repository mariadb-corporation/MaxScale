#ifndef _HOUSEKEEPER_H
#define _HOUSEKEEPER_H
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
 * Copyright MariaDB Corporation Ab 2014
 */
#include <time.h>
#include <dcb.h>
#include <hk_heartbeat.h>
/**
 * @file housekeeper.h A mechanism to have task run periodically
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 29/08/14     Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */

typedef enum
{
    HK_REPEATED = 1,
    HK_ONESHOT
} HKTASK_TYPE;

/**
 * The housekeeper task list
 */
typedef struct hktask
{
    char *name;               /*< A simple task name */
    void (*task)(void *data); /*< The task to call */
    void *data;               /*< Data to pass the task */
    int frequency;            /*< How often to call the tasks (seconds) */
    time_t nextdue;           /*< When the task should be next run */
    HKTASK_TYPE type;         /*< The task type */
    struct hktask *next;      /*< Next task in the list */
} HKTASK;

extern void hkinit();
extern int  hktask_add(char *name, void (*task)(void *), void *data, int frequency);
extern int  hktask_oneshot(char *name, void (*task)(void *), void *data, int when);
extern int  hktask_remove(char *name);
extern void hkshutdown();
extern void hkshow_tasks(DCB *pdcb);

#endif

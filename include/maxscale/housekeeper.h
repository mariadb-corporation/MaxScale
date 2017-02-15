#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file housekeeper.h A mechanism to have task run periodically
 */

#include <maxscale/cdefs.h>
#include <time.h>
#include <maxscale/dcb.h>
#include <maxscale/hk_heartbeat.h>

MXS_BEGIN_DECLS

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

/**
 * Initialises the housekeeper mechanism.
 *
 * A call to any of the other housekeeper functions can be made only if
 * this function returns successfully.
 *
 * @return True if the housekeeper mechanism was initialized, false otherwise.
 */
extern bool hkinit();

/**
 * Shuts down the housekeeper mechanism.
 *
 * Should be called @b only if @c hkinit() returned successfully.
 *
 * @see hkinit hkfinish
 */
extern void hkshutdown();

/**
 * Waits for the housekeeper thread to finish. Should be called only after
 * hkshutdown() has been called.
 */
extern void hkfinish();

extern int  hktask_add(const char *name, void (*task)(void *), void *data, int frequency);
extern int  hktask_oneshot(const char *name, void (*task)(void *), void *data, int when);
extern int  hktask_remove(const char *name);
extern void hkshow_tasks(DCB *pdcb);

MXS_END_DECLS

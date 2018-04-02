#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
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

MXS_BEGIN_DECLS

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

/**
 * @brief Add a new task
 *
 * The task will be first run @c frequency seconds after this call is
 * made and will the be executed repeatedly every frequency seconds
 * until the task is removed.
 *
 * Task names must be unique.
 *
 * @param name      Task name
 * @param task      Function to execute
 * @param data      Data passed to function as the parameter
 * @param frequency Frequency of execution
 *
 * @return 1 if task was added
 */
int hktask_add(const char *name, void (*task)(void *) , void *data, int frequency);

/**
 * @brief Add oneshot task
 *
 * The task will only execute once.
 *
 * @param name Task name
 * @param task Function to execute
 * @param data Data passed to function as the parameter
 * @param when Number of seconds to wait until task is executed
 *
 * @return 1 if task was added
 */
int hktask_oneshot(const char *name, void (*task)(void *) , void *data, int when);

/**
 * @brief Remove a task
 *
 * @param name Task name
 *
 * @return 1 if the task was removed
 */
int hktask_remove(const char *name);

/**
 * @brief Show the tasks that are scheduled for the house keeper
 *
 * @param pdcb The DCB to send to output
 */
void hkshow_tasks(DCB *pdcb);

/**
 * @brief Show tasks as JSON resource
 *
 * @param host Hostname of this server
 *
 * @return Collection of JSON formatted task resources
 */
json_t* hk_tasks_json(const char* host);

MXS_END_DECLS

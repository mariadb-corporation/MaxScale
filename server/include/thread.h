#ifndef _THREAD_H
#define _THREAD_H
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
#include <pthread.h>

/**
 * @file thread.h       The gateway threading interface
 *
 * An encapsulation of the threading used by the gateway. This is designed to
 * isolate the majority of the gateway code from th epthread library, enabling
 * the gateway to be ported to a different threading package with the minimum
 * of changes.
 */

#define THREAD       pthread_t
#define THREAD_SHELF pthread_self

extern void *thread_start(void (*entry)(void *), void *arg);
extern void thread_wait(void *thd);
extern void thread_millisleep(int ms);

#endif

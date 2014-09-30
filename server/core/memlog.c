/*
 * This file is distributed as part of the MariaDB MaxScale.  It is free
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
 * Copyright MariaDB Ab 2014
 */

/**
 * @file memlog.c  - Implementation of memory logging mechanism for debug purposes
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 26/09/14	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include <memlog.h>
#include <stdio.h>

static	MEMLOG		*memlogs = NULL;
static	SPINLOCK	*memlock = SPINLOCK_INIT;

static	void	memlog_flush(MEMLOG *);

/**
 * Create a new instance of a memory logger.
 *
 * @param name	The name of the memory log
 * @param type	The type of item being logged
 * @param size	The number of items to store in memory before flushign to disk
 *
 * @return MEMLOG*	A memory log handle
 */
MEMLOG *
memlog_create(char *name, MEMLOGTYPE type, int size)
{
MEMLOG	*log;

	if ((log = (MEMLOG *)malloc(sizeof(MEMLOG))) == NULL)
	{
		return NULL;
	}

	log->name = strdup(name);
	spinlock_init(&log->lock);
	log->type = type;
	log->offset = 0;
	log->size = size;
	switch (type)
	{
	case ML_INT:
		log->values = malloc(sizeof(int) * size);
		break;
	case ML_LONG:
		log->values = malloc(sizeof(long) * size);
		break;
	case ML_LONGLONG:
		log->values = malloc(sizeof(long long) * size);
		break;
	case ML_STRING:
		log->values = malloc(sizeof(char *) * size);
		break;
	}
	if (log->values == NULL)
	{
		free(log);
		return NULL;
	}
	spinlock_acquire(&memlock);
	log->next = memlogs;
	memlogs = log;
	spinlock_release(&memlock);

	return log;
}

/**
 * Destroy a memory logger any unwritten data will be flushed to disk
 *
 * @param log	The memory log to destroy
 */
void
memlog_destroy(MEMLOG *log)
{
MEMLOG *ptr;

	memlog_flush(log);
	free(log->values);

	spinlock_acquire(&memlock);
	if (memlogs == log)
		memlogs = log->next;
	else
	{
		ptr = memlogs;
		while (ptr && ptr->next != log)
			ptr = ptr->next;
		if (ptr)
			ptr->next = log->next;
	}
	spinlock_release(&memlock);
	free(log->name);
	free(log);
}

/**
 * Log a data item to the memory logger
 *
 * @param log	The memory logger
 * @param value	The value to log
 */
void
memlog_log(MEMLOG *log, void *value)
{
	spinlock_acquire(&log->lock);
	switch (log->type)
	{
	case ML_INT:
		((int *)(log->values))[log->offset] = (int)value;
		break;
	case ML_LONG:
		((long *)(log->values))[log->offset] = (long)value;
		break;
	case ML_LONGLONG:
		((long long *)(log->values))[log->offset] = (long long)value;
		break;
	case ML_STRING:
		((char **)(log->values))[log->offset] = (char *)value;
		break;
	}
	log->offset++;
	if (log->offset == log->size)
	{
		memlog_flush(log);
		log->offset = 0;
	}
	spinlock_release(&log->lock);
}

/**
 * Flush all memlogs to disk, called during shutdown
 *
 */
void
memlog_flush_all()
{
MEMLOG	*log;

	spinlock_acquire(&memlock);
	log = memlogs;
	while (log)
	{
		spinlock_acquire(&log->lock);
		memlog_flush(log);
		spinlock_release(&log->lock);
		log = log->next;
	}
	spinlock_release(&memlock);
}


/**
 * Flush a memory log to disk
 *
 * Assumes the the log->lock has been acquired by the caller
 *
 * @param log	The memory log to flush
 */
static void
memlog_flush(MEMLOG *log)
{
FILE	*fp;
int	i;

	if ((fp = fopen(log->name, "a")) == NULL)
		return;
	for (i = 0; i < log->offset; i++)
	{
		switch (log->type)
		{
		case ML_INT:
			fprintf(fp, "%d\n", ((int *)(log->values))[i]);
			break;
		case ML_LONG:
			fprintf(fp, "%ld\n", ((long *)(log->values))[i]);
			break;
		case ML_LONGLONG:
			fprintf(fp, "%lld\n", ((long long *)(log->values))[i]);
			break;
		case ML_STRING:
			fprintf(fp, "%s\n", ((char **)(log->values))[i]);
			break;
		}
	}
	fclose(fp);
}

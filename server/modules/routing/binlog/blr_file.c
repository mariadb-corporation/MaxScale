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

/**
 * @file blr_file.c - contains code for the router binlog file management
 *
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 14/04/2014	Mark Riddoch		Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <service.h>
#include <server.h>
#include <router.h>
#include <atomic.h>
#include <spinlock.h>
#include <blr.h>
#include <dcb.h>
#include <spinlock.h>

#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>


extern int lm_enabled_logfiles_bitmask;

static void blr_file_create(ROUTER_INSTANCE *router, char *file);
static void blr_file_append(ROUTER_INSTANCE *router, char *file);
static uint32_t extract_field(uint8_t *src, int bits);

/**
 * Initialise the binlog file for this instance. MaxScale will look
 * for all the binlogs that it has on local disk, determien the next
 * binlog to use and initialise it for writing, determining the 
 * next record to be fetched from the real master.
 *
 * @param router	The router instance this defines the master for this replication chain
 */
void
blr_file_init(ROUTER_INSTANCE *router)
{
char		*ptr, path[1024], filename[1050];
int		file_found, n = 1;
int		root_len, i;
DIR		*dirp;
struct dirent	*dp;

	strcpy(path, "/usr/local/skysql/MaxScale");
	if ((ptr = getenv("MAXSCALE_HOME")) != NULL)
	{
		strcpy(path, ptr);
	}
	strcat(path, "/");
	strcat(path, router->service->name);

	if (access(path, R_OK) == -1)
		mkdir(path, 0777);

	/* First try to find a binlog file number by reading the directory */
	root_len = strlen(router->fileroot);
	dirp = opendir(path);
	while ((dp = readdir(dirp)) != NULL)
	{
		if (strncmp(dp->d_name, router->fileroot, root_len) == 0)
		{
			i = atoi(dp->d_name + root_len + 1);
			if (i > n)
				n = i;
		}
	}
	closedir(dirp);


	file_found = 0;
	do {
		sprintf(filename, "%s/" BINLOG_NAMEFMT, path, router->fileroot, n);
		if (access(filename, R_OK) != -1)
		{
			file_found  = 1;
			n++;
		}
		else
			file_found = 0;
	} while (file_found);
	n--;

	if (n == 0)		// No binlog files found
	{
		if (router->initbinlog)
			sprintf(filename, BINLOG_NAMEFMT, router->fileroot,
						router->initbinlog);
		else
			sprintf(filename, BINLOG_NAMEFMT, router->fileroot, 1);
		blr_file_create(router, filename);
	}
	else
	{
		sprintf(filename, BINLOG_NAMEFMT, router->fileroot, n);
		blr_file_append(router, filename);
	}
	
}

void
blr_file_rotate(ROUTER_INSTANCE *router, char *file, uint64_t pos)
{
	blr_file_create(router, file);
}


/**
 * Create a new binlog file for the router to use.
 *
 * @param router	The router instance
 * @param file		The binlog file name
 */
static void
blr_file_create(ROUTER_INSTANCE *router, char *file)
{
char		*ptr, path[1024];
int		fd;
unsigned char	magic[] = BINLOG_MAGIC;

	strcpy(path, "/usr/local/skysql/MaxScale");
	if ((ptr = getenv("MAXSCALE_HOME")) != NULL)
	{
		strcpy(path, ptr);
	}
	strcat(path, "/");
	strcat(path, router->service->name);
	strcat(path, "/");
	strcat(path, file);

	if ((fd = open(path, O_RDWR|O_CREAT, 0666)) != -1)
	{
		write(fd, magic, 4);
	}
	else
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Failed to create binlog file %s\n", path)));
	}
	fsync(fd);
	close(router->binlog_fd);
	strcpy(router->binlog_name, file);
	router->binlog_position = 4;			/* Initial position after the magic number */
	router->binlog_fd = fd;
}


/**
 * Prepare an existing binlog file to be appened to.
 *
 * @param router	The router instance
 * @param file		The binlog file name
 */
static void
blr_file_append(ROUTER_INSTANCE *router, char *file)
{
char		*ptr, path[1024];
int		fd;

	strcpy(path, "/usr/local/skysql/MaxScale");
	if ((ptr = getenv("MAXSCALE_HOME")) != NULL)
	{
		strcpy(path, ptr);
	}
	strcat(path, "/");
	strcat(path, router->service->name);
	strcat(path, "/");
	strcat(path, file);

	if ((fd = open(path, O_RDWR|O_APPEND, 0666)) == -1)
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Failed to open binlog file %s for append.\n",
				path)));
		return;
	}
	fsync(fd);
	close(router->binlog_fd);
	strcpy(router->binlog_name, file);
	router->binlog_position = lseek(fd, 0L, SEEK_END);
	router->binlog_fd = fd;
}

/**
 * Write a binlog entry to disk.
 *
 * @param router	The router instance
 * @param buf		The binlog record
 * @param len		The length of the binlog record
 */
void
blr_write_binlog_record(ROUTER_INSTANCE *router, REP_HEADER *hdr, uint8_t *buf)
{
	pwrite(router->binlog_fd, buf, hdr->event_size, hdr->next_pos - hdr->event_size);
	router->binlog_position = hdr->next_pos;
}

/**
 * Flush the content of the binlog file to disk.
 *
 * @param	router		The binlog router
 */
void
blr_file_flush(ROUTER_INSTANCE *router)
{
	fsync(router->binlog_fd);
}

int
blr_open_binlog(ROUTER_INSTANCE *router, char *binlog)
{
char		*ptr, path[1024];
int		rval;

	strcpy(path, "/usr/local/skysql/MaxScale");
	if ((ptr = getenv("MAXSCALE_HOME")) != NULL)
	{
		strcpy(path, ptr);
	}
	strcat(path, "/");
	strcat(path, router->service->name);
	strcat(path, "/");
	strcat(path, binlog);

	if ((rval = open(path, O_RDONLY, 0666)) == -1)
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Failed to open binlog file %s\n", path)));
	}

	return rval;
}

/**
 * Read a replication event into a GWBUF structure.
 *
 * @param fd	File descriptor of the binlog file
 * @param pos	Position of binlog record to read
 * @param hdr	Binlog header to populate
 * @return	The binlog record wrapped in a GWBUF structure
 */
GWBUF *
blr_read_binlog(int fd, unsigned int pos, REP_HEADER *hdr)
{
uint8_t		hdbuf[19];
GWBUF		*result;
unsigned char	*data;
int		n;

	if (lseek(fd, pos, SEEK_SET) != pos)
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Failed to seek for binlog entry, "
			"at %d.\n", pos)));
		return NULL;
	}

	/* Read the header information from the file */
	if ((n = read(fd, hdbuf, 19)) != 19)
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Failed to read header for binlog entry, "
			"at %d (%s).\n", pos, strerror(errno))));
		if (n> 0 && n < 19)
			LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
				"Short read when reading the header. "
				"Expected 19 bytes got %d bytes.\n",
				n)));
		return NULL;
	}
	hdr->timestamp = extract_field(hdbuf, 32);
	hdr->event_type = hdbuf[4];
	hdr->serverid = extract_field(&hdbuf[5], 32);
	hdr->event_size = extract_field(&hdbuf[9], 32);
	hdr->next_pos = extract_field(&hdbuf[13], 32);
	hdr->flags = extract_field(&hdbuf[17], 16);
	if ((result = gwbuf_alloc(hdr->event_size)) == NULL)
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Failed to allocate memory for binlog entry, "
                        "size %d at %d.\n",
                        hdr->event_size, pos)));
		return NULL;
	}
	data = GWBUF_DATA(result);
	memcpy(data, hdbuf, 19);	// Copy the header in
	if ((n = read(fd, &data[19], hdr->event_size - 19))
			!= hdr->event_size - 19)	// Read the balance
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Short read when reading the event at %d. "
				"Expected %d bytes got %d bytes.\n",
				pos, n)));
		gwbuf_consume(result, hdr->event_size);
		return NULL;
	}
	return result;
}

/** 
 * Extract a numeric field from a packet of the specified number of bits
 *
 * @param src	The raw packet source
 * @param birs	The number of bits to extract (multiple of 8)
 */
static uint32_t
extract_field(uint8_t *src, int bits)
{
uint32_t	rval = 0, shift = 0;

	while (bits > 0)
	{
		rval |= (*src++) << shift;
		shift += 8;
		bits -= 8;
	}
	return rval;
}

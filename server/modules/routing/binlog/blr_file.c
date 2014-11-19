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
static void blr_log_header(logfile_id_t file, char *msg, uint8_t *ptr);

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

	router->binlogdir = strdup(path);

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
char		path[1024];
int		fd;
unsigned char	magic[] = BINLOG_MAGIC;

	strcpy(path, router->binlogdir);
	strcat(path, "/");
	strcat(path, file);

	if ((fd = open(path, O_RDWR|O_CREAT, 0666)) != -1)
	{
		write(fd, magic, 4);
	}
	else
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Failed to create binlog file %s", path)));
	}
	fsync(fd);
	close(router->binlog_fd);
	spinlock_acquire(&router->binlog_lock);
	strcpy(router->binlog_name, file);
	router->binlog_position = 4;			/* Initial position after the magic number */
	spinlock_release(&router->binlog_lock);
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
char		path[1024];
int		fd;

	strcpy(path, router->binlogdir);
	strcat(path, "/");
	strcat(path, file);

	if ((fd = open(path, O_RDWR|O_APPEND, 0666)) == -1)
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Failed to open binlog file %s for append.",
				path)));
		return;
	}
	fsync(fd);
	close(router->binlog_fd);
	spinlock_acquire(&router->binlog_lock);
	strcpy(router->binlog_name, file);
	router->binlog_position = lseek(fd, 0L, SEEK_END);
	spinlock_release(&router->binlog_lock);
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
	spinlock_acquire(&router->binlog_lock);
	router->binlog_position = hdr->next_pos;
	router->last_written = hdr->next_pos - hdr->event_size;
	spinlock_release(&router->binlog_lock);
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

/**
 * Open a binlog file for reading binlog records
 *
 * @param router	The router instance
 * @param binlog	The binlog filename
 * @return a binlog file record
 */
BLFILE *
blr_open_binlog(ROUTER_INSTANCE *router, char *binlog)
{
char		*ptr, path[1024];
BLFILE		*file;

	spinlock_acquire(&router->fileslock);
	file = router->files;
	while (file && strcmp(file->binlogname, binlog) != 0)
		file = file->next;

	if (file)
	{
		file->refcnt++;
		spinlock_release(&router->fileslock);
		return file;
	}

	if ((file = (BLFILE *)malloc(sizeof(BLFILE))) == NULL)
	{
		spinlock_release(&router->fileslock);
		return NULL;
	}
	strcpy(file->binlogname, binlog);
	file->refcnt = 1;
	file->cache = 0;
	spinlock_init(&file->lock);

	strcpy(path, router->binlogdir);
	strcat(path, "/");
	strcat(path, binlog);

	if ((file->fd = open(path, O_RDONLY, 0666)) == -1)
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Failed to open binlog file %s", path)));
		free(file);
		spinlock_release(&router->fileslock);
		return NULL;
	}

	file->next = router->files;
	router->files = file;
	spinlock_release(&router->fileslock);

	return file;
}

/**
 * Read a replication event into a GWBUF structure.
 *
 * @param router	The router instance
 * @param file		File record
 * @param pos		Position of binlog record to read
 * @param hdr		Binlog header to populate
 * @return		The binlog record wrapped in a GWBUF structure
 */
GWBUF *
blr_read_binlog(ROUTER_INSTANCE *router, BLFILE *file, unsigned int pos, REP_HEADER *hdr)
{
uint8_t		hdbuf[19];
GWBUF		*result;
unsigned char	*data;
int		n;
unsigned long	filelen = 0;
struct	stat	statb;

	if (fstat(file->fd, &statb) == 0)
		filelen = statb.st_size;
	if (pos >= filelen)
	{
		LOGIF(LD, (skygw_log_write(LOGFILE_ERROR,
			"Attempting to read off the end of the binlog file %s, "
			"event at %lu.", file->binlogname, pos)));
		return NULL;
	}

	if (strcmp(router->binlog_name, file->binlogname) == 0 &&
			pos >= router->binlog_position)
	{
		return NULL;
	}
		

	/* Read the header information from the file */
	if ((n = pread(file->fd, hdbuf, 19, pos)) != 19)
	{
		switch (n)
		{
		case 0:
			LOGIF(LD, (skygw_log_write(LOGFILE_DEBUG,
				"Reached end of binlog file at %d.",
					pos)));
			break;
		case -1:
			LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
				"Failed to read binlog file %s at position %d"
				" (%s).", file->binlogname,
						pos, strerror(errno))));
			if (errno == EBADF)
				LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
					"Bad file descriptor in read binlog for file %s"
					", reference count is %d, descriptor %d.",
						file->binlogname, file->refcnt, file->fd)));
			break;
		default:
			LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
				"Short read when reading the header. "
				"Expected 19 bytes but got %d bytes. "
				"Binlog file is %s, position %d",
				file->binlogname, pos, n)));
			break;
		}
		return NULL;
	}
	hdr->timestamp = EXTRACT32(hdbuf);
	hdr->event_type = hdbuf[4];
	hdr->serverid = EXTRACT32(&hdbuf[5]);
	hdr->event_size = extract_field(&hdbuf[9], 32);
	hdr->next_pos = EXTRACT32(&hdbuf[13]);
	hdr->flags = EXTRACT16(&hdbuf[17]);
	if (hdr->next_pos < pos && hdr->event_type != ROTATE_EVENT)
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Next position in header appears to be incorrect "
			"rereading event header at pos %ul in file %s, "
			"file size is %ul. Master will write %ul in %s next.",
			pos, file->binlogname, filelen, router->binlog_position,
			router->binlog_name)));
		if ((n = pread(file->fd, hdbuf, 19, pos)) != 19)
		{
			switch (n)
			{
			case 0:
				LOGIF(LD, (skygw_log_write(LOGFILE_DEBUG,
					"Reached end of binlog file at %d.",
						pos)));
				break;
			case -1:
				LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
					"Failed to read binlog file %s at position %d"
					" (%s).", file->binlogname,
							pos, strerror(errno))));
				if (errno == EBADF)
					LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
						"Bad file descriptor in read binlog for file %s"
						", reference count is %d, descriptor %d.",
							file->binlogname, file->refcnt, file->fd)));
				break;
			default:
				LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
					"Short read when reading the header. "
					"Expected 19 bytes but got %d bytes. "
					"Binlog file is %s, position %d",
					file->binlogname, pos, n)));
				break;
			}
			return NULL;
		}
		hdr->timestamp = EXTRACT32(hdbuf);
		hdr->event_type = hdbuf[4];
		hdr->serverid = EXTRACT32(&hdbuf[5]);
		hdr->event_size = extract_field(&hdbuf[9], 32);
		hdr->next_pos = EXTRACT32(&hdbuf[13]);
		hdr->flags = EXTRACT16(&hdbuf[17]);

		if (hdr->next_pos < pos && hdr->event_type != ROTATE_EVENT)
		{
			LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
				"Next position still incorrect after "
				"rereading")));
			return NULL;
		}
		else
		{
			LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
				"Next position corrected by "
				"rereading")));
		}
	}
	if ((result = gwbuf_alloc(hdr->event_size)) == NULL)
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Failed to allocate memory for binlog entry, "
                        "size %d at %d.",
                        hdr->event_size, pos)));
		return NULL;
	}
	data = GWBUF_DATA(result);
	memcpy(data, hdbuf, 19);	// Copy the header in
	if ((n = pread(file->fd, &data[19], hdr->event_size - 19, pos + 19))
			!= hdr->event_size - 19)	// Read the balance
	{
		if (n == -1)
		{
			LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
				"Error reading the event at %ld in %s. "
				"%s, expected %d bytes.",
				pos, file->binlogname, 
				strerror(errno), hdr->event_size - 19)));
		}
		else
		{
			LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
				"Short read when reading the event at %ld in %s. "
				"Expected %d bytes got %d bytes.",
				pos, file->binlogname, hdr->event_size - 19, n)));
			if (filelen != 0 && filelen - pos < hdr->event_size)
			{
				LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
					"Binlog event is close to the end of the binlog file, "
					"current file size is %u.",
					filelen)));
			}
			blr_log_header(LOGFILE_ERROR, "Possible malformed event header", hdbuf);
		}
		gwbuf_consume(result, hdr->event_size);
		return NULL;
	}
	return result;
}

/**
 * Close a binlog file that has been opened to read binlog records
 *
 * The open binlog files are shared between multiple slaves that are
 * reading the same binlog file.
 *
 * @param router	The router instance
 * @param file		The file to close
 */
void
blr_close_binlog(ROUTER_INSTANCE *router, BLFILE *file)
{
	spinlock_acquire(&file->lock);
	file->refcnt--;
	if (file->refcnt == 0)
	{
		spinlock_acquire(&router->fileslock);
		if (router->files == file)
			router->files = file->next;
		else
		{
			BLFILE	*ptr = router->files;
			while (ptr && ptr->next != file)
				ptr = ptr->next;
			if (ptr)
				ptr->next = file->next;
		}
		spinlock_release(&router->fileslock);

		close(file->fd);
		file->fd = -1;
	}
	spinlock_release(&file->lock);
	if (file->refcnt == 0)
		free(file);
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

/**
 * Log the event header of  binlog event
 *
 * @param	file	The log file into which to write the entry
 * @param 	msg	A message strign to preceed the header with
 * @param	ptr	The event header raw data
 */
static void
blr_log_header(logfile_id_t file, char *msg, uint8_t *ptr)
{
char	buf[400], *bufp;
int	i;

	bufp = buf;
	bufp += sprintf(bufp, "%s: ", msg);
	for (i = 0; i < 19; i++)
		bufp += sprintf(bufp, "0x%02x ", ptr[i]);
	skygw_log_write_flush(file, "%s", buf);
	
}

/**
 * Return the size of the current binlog file
 *
 * @param file	The binlog file
 * @return	The current size of the binlog file
 */
unsigned long
blr_file_size(BLFILE *file)
{
struct	stat	statb;

	if (fstat(file->fd, &statb) == 0)
		return statb.st_size;
	return 0;
}

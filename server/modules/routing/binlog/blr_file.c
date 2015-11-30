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
 * Copyright MariaDB Corporation Ab 2014-2015
 */

/**
 * @file blr_file.c - contains code for the router binlog file management
 *
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 14/04/2014	Mark Riddoch		Initial implementation
 * 07/05/2015	Massimiliano Pinto	Added MAX_EVENT_TYPE_MARIADB10
 * 08/06/2015	Massimiliano Pinto	Addition of blr_cache_read_master_data()
 * 15/06/2015	Massimiliano Pinto	Addition of blr_file_get_next_binlogname()
 * 23/06/2015	Massimiliano Pinto	Addition of blr_file_use_binlog, blr_file_create_binlog
 * 29/06/2015	Massimiliano Pinto	Addition of blr_file_write_master_config()
 *					Cache directory is now 'cache' under router->binlogdir
 * 05/08/2015	Massimiliano Pinto	Initial implementation of transaction safety
 * 24/08/2015	Massimiliano Pinto	Added strerror_r
 * 26/08/2015	Massimiliano Pinto	Added MariaDB 10 GTID event check with flags = 0
 *					This is the current supported condition for detecting
 *					MariaDB 10 transaction start point.
 *					It's no longer using QUERY_EVENT with BEGIN
 * 23/10/15	Markus Makela		Added current_safe_event
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
#include <gwdirs.h>
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

static int  blr_file_create(ROUTER_INSTANCE *router, char *file);
static void blr_file_append(ROUTER_INSTANCE *router, char *file);
static void blr_log_header(int priority, char *msg, uint8_t *ptr);
void blr_cache_read_master_data(ROUTER_INSTANCE *router);
int blr_file_get_next_binlogname(ROUTER_INSTANCE *router);
int blr_file_new_binlog(ROUTER_INSTANCE *router, char *file);
void blr_file_use_binlog(ROUTER_INSTANCE *router, char *file);
int blr_file_write_master_config(ROUTER_INSTANCE *router, char *error);
extern uint32_t extract_field(uint8_t *src, int bits);
static void blr_format_event_size(double *event_size, char *label);
extern int MaxScaleUptime();
extern char *blr_get_event_description(ROUTER_INSTANCE *router, uint8_t event);

typedef struct binlog_event_desc {
	unsigned long long event_pos;
	uint8_t	event_type;
	time_t	event_time;
} BINLOG_EVENT_DESC;

static void blr_print_binlog_details(ROUTER_INSTANCE *router, BINLOG_EVENT_DESC first_event_time, BINLOG_EVENT_DESC last_event_time);

/**
 * Initialise the binlog file for this instance. MaxScale will look
 * for all the binlogs that it has on local disk, determine the next
 * binlog to use and initialise it for writing, determining the 
 * next record to be fetched from the real master.
 *
 * @param router	The router instance this defines the master for this replication chain
 */
int
blr_file_init(ROUTER_INSTANCE *router)
{
char		*ptr;
char 		path[PATH_MAX+1] = "";
char		filename[PATH_MAX+1] = "";
int		file_found, n = 1;
int		root_len, i;
DIR		*dirp;
struct dirent	*dp;

	if (router->binlogdir == NULL)
	{
		strncpy(path, get_datadir(), PATH_MAX);
		strncat(path,"/",PATH_MAX);
		strncat(path, router->service->name,PATH_MAX);

		if (access(path, R_OK) == -1)
			mkdir(path, 0700);

		router->binlogdir = strdup(path);
	}
	else
	{
		strncpy(path, router->binlogdir, PATH_MAX);
	}
	if (access(router->binlogdir, R_OK) == -1)
	{
		MXS_ERROR("%s: Unable to read the binlog directory %s.",
                          router->service->name, router->binlogdir);
		return 0;
	}

	/* First try to find a binlog file number by reading the directory */
	root_len = strlen(router->fileroot);
	if ((dirp = opendir(path)) == NULL)
	{
		char err_msg[BLRM_STRERROR_R_MSG_SIZE];
		MXS_ERROR("%s: Unable to read the binlog directory %s, %s.",
                          router->service->name, router->binlogdir,
                          strerror_r(errno, err_msg, sizeof(err_msg)));
		return 0;
	}
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
		snprintf(filename,PATH_MAX, "%s/" BINLOG_NAMEFMT, path, router->fileroot, n);
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
			snprintf(filename,PATH_MAX, BINLOG_NAMEFMT, router->fileroot,
						router->initbinlog);
		else
			snprintf(filename,PATH_MAX, BINLOG_NAMEFMT, router->fileroot, 1);
		if (! blr_file_create(router, filename))
			return 0;
	}
	else
	{
		snprintf(filename,PATH_MAX, BINLOG_NAMEFMT, router->fileroot, n);
		blr_file_append(router, filename);
	}
	return 1;
}

int
blr_file_rotate(ROUTER_INSTANCE *router, char *file, uint64_t pos)
{
	return blr_file_create(router, file);
}


/**
 * binlog files need an initial 4 magic bytes at the start. blr_file_add_magic()
 * adds them.
 *
 * @param router	The router instance
 * @param fd		file descriptor to the open binlog file
 * @return		Nothing
 */
static void
blr_file_add_magic(ROUTER_INSTANCE *router, int fd)
{
unsigned char	magic[] = BINLOG_MAGIC;

	write(fd, magic, 4);
	router->current_pos = 4;			/* Initial position after the magic number */
	router->binlog_position = 4;			/* Initial position after the magic number */
	router->current_safe_event = 4;
	router->last_written = 0;
}


/**
 * Create a new binlog file for the router to use.
 *
 * @param router	The router instance
 * @param file		The binlog file name
 * @return		Non-zero if the fie creation succeeded
 */
static int
blr_file_create(ROUTER_INSTANCE *router, char *file)
{
char		path[PATH_MAX + 1] = "";
int		fd;

	strcpy(path, router->binlogdir);
	strcat(path, "/");
	strcat(path, file);

	if ((fd = open(path, O_RDWR|O_CREAT, 0666)) != -1)
	{
		blr_file_add_magic(router,fd);
	}
	else
	{
		char err_msg[STRERROR_BUFLEN];

		MXS_ERROR("%s: Failed to create binlog file %s, %s.",
                          router->service->name, path, strerror_r(errno, err_msg, sizeof(err_msg)));
		return 0;
	}
	fsync(fd);
	close(router->binlog_fd);
	spinlock_acquire(&router->binlog_lock);
	strncpy(router->binlog_name, file, BINLOG_FNAMELEN);
	router->binlog_fd = fd;
	spinlock_release(&router->binlog_lock);
	return 1;
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
char		path[PATH_MAX+1] = "";
int		fd;

	strcpy(path, router->binlogdir);
	strcat(path, "/");
	strcat(path, file);

	if ((fd = open(path, O_RDWR|O_APPEND, 0666)) == -1)
	{
		MXS_ERROR("Failed to open binlog file %s for append.",
                          path);
		return;
	}
	fsync(fd);
	close(router->binlog_fd);
	spinlock_acquire(&router->binlog_lock);
	memmove(router->binlog_name, file, BINLOG_FNAMELEN);
	router->current_pos = lseek(fd, 0L, SEEK_END);
	if (router->current_pos < 4) {
		if (router->current_pos == 0) {
			blr_file_add_magic(router, fd);
		} else {
			/* If for any reason the file's length is between 1 and 3 bytes
			 * then report an error. */
	                MXS_ERROR("%s: binlog file %s has an invalid length %lu.",
                                  router->service->name, path, router->current_pos);
			close(fd);
			spinlock_release(&router->binlog_lock);
			return;
		}
	}
	router->binlog_fd = fd;
	spinlock_release(&router->binlog_lock);
}

/**
 * Write a binlog entry to disk.
 *
 * @param router	The router instance
 * @param buf		The binlog record
 * @param len		The length of the binlog record
 * @return 		Return the number of bytes written
 */
int
blr_write_binlog_record(ROUTER_INSTANCE *router, REP_HEADER *hdr, uint8_t *buf)
{
int	n;

	if ((n = pwrite(router->binlog_fd, buf, hdr->event_size,
				hdr->next_pos - hdr->event_size)) != hdr->event_size)
	{
		char err_msg[STRERROR_BUFLEN];
		MXS_ERROR("%s: Failed to write binlog record at %d of %s, %s. "
                          "Truncating to previous record.",
                          router->service->name, hdr->next_pos - hdr->event_size,
                          router->binlog_name,
                          strerror_r(errno, err_msg, sizeof(err_msg)));
		/* Remove any partual event that was written */
		ftruncate(router->binlog_fd, hdr->next_pos - hdr->event_size);
		return 0;
	}
	spinlock_acquire(&router->binlog_lock);
	router->current_pos = hdr->next_pos;
	router->last_written = hdr->next_pos - hdr->event_size;
	spinlock_release(&router->binlog_lock);
	return n;
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
char		path[PATH_MAX + 1] = "";
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

	if ((file = (BLFILE *)calloc(1, sizeof(BLFILE))) == NULL)
	{
		spinlock_release(&router->fileslock);
		return NULL;
	}
	strncpy(file->binlogname, binlog, BINLOG_FNAMELEN);
	file->refcnt = 1;
	file->cache = 0;
	spinlock_init(&file->lock);

	strncpy(path, router->binlogdir, PATH_MAX);
	strncat(path, "/", PATH_MAX);
	strncat(path, binlog, PATH_MAX);

	if ((file->fd = open(path, O_RDONLY, 0666)) == -1)
	{
		MXS_ERROR("Failed to open binlog file %s", path);
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
 * @param errmsg	Allocated BINLOG_ERROR_MSG_LEN bytes message error buffer
 * @return		The binlog record wrapped in a GWBUF structure
 */
GWBUF *
blr_read_binlog(ROUTER_INSTANCE *router, BLFILE *file, unsigned long pos, REP_HEADER *hdr, char *errmsg)
{
uint8_t		hdbuf[BINLOG_EVENT_HDR_LEN];
GWBUF		*result;
unsigned char	*data;
int	n;
unsigned long	filelen = 0;
struct	stat	statb;

	memset(&hdbuf, '\0', BINLOG_EVENT_HDR_LEN);

	/* set error indicator */
	hdr->ok = SLAVE_POS_READ_ERR;

	if (!file)
	{
		snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Invalid file pointer for requested binlog at position %lu", pos);
		return NULL;
	}

	spinlock_acquire(&file->lock);
	if (fstat(file->fd, &statb) == 0)
		filelen = statb.st_size;
	else {
		if (file->fd == -1) {
			hdr->ok = SLAVE_POS_BAD_FD;
			snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "blr_read_binlog called with invalid file->fd, pos %lu", pos);
			spinlock_release(&file->lock);
			return NULL;
		}
	}
        spinlock_release(&file->lock);

	if (pos > filelen)
	{
		snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Requested position %lu is beyond end of the binlog file '%s', size %lu",
			pos, file->binlogname, filelen);
		return NULL;
	}

	spinlock_acquire(&router->binlog_lock);
	spinlock_acquire(&file->lock);

	if (strcmp(router->binlog_name, file->binlogname) == 0 &&
			pos >= router->binlog_position)
	{
		if (pos > router->binlog_position && !router->rotating)
		{
			/* Unsafe position, slave will be disconnected by the calling routine */
			snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Requested binlog position %lu. Position is unsafe so disconnecting. "
				"Latest safe position %lu, end of binlog file %lu",
				pos, router->binlog_position, router->current_pos);

			hdr->ok = SLAVE_POS_READ_UNSAFE;
		} else {
			/* accessing last position is ok */
			hdr->ok = SLAVE_POS_READ_OK;
		}

		spinlock_release(&file->lock);
		spinlock_release(&router->binlog_lock);

		return NULL;
	}
	spinlock_release(&file->lock);
	spinlock_release(&router->binlog_lock);

	/* Read the header information from the file */
	if ((n = pread(file->fd, hdbuf, BINLOG_EVENT_HDR_LEN, pos)) != BINLOG_EVENT_HDR_LEN)
	{
		switch (n)
		{
		case 0:
                    MXS_DEBUG("Reached end of binlog file '%s' at %lu.",
                              file->binlogname, pos);

			/* set ok indicator */
			hdr->ok = SLAVE_POS_READ_OK;

			break;
		case -1:
			{
			char err_msg[STRERROR_BUFLEN];
			snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Failed to read binlog file '%s'; (%s), event at %lu",
				file->binlogname, strerror_r(errno, err_msg, sizeof(err_msg)), pos);

			if (errno == EBADF)
				snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Bad file descriptor for binlog file '%s', refcount %d, descriptor %d, event at %lu",
					file->binlogname, file->refcnt, file->fd, pos);
			break;
			}
		default:
			snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Bogus data in log event header; "
				"expected %d bytes but read %d, position %lu, binlog file '%s'",
				 BINLOG_EVENT_HDR_LEN, n, pos, file->binlogname);
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

	/* event pos & size checks */
	if (hdr->event_size == 0 || ((hdr->next_pos != (pos + hdr->event_size)) && (hdr->event_type != ROTATE_EVENT))) {
		snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Client requested master to start replication from invalid position %lu in binlog file '%s'", pos, file->binlogname);
                return NULL;
        }

	/* event type checks */
	if (router->mariadb10_compat) {
		if (hdr->event_type > MAX_EVENT_TYPE_MARIADB10) {
			snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Invalid MariaDB 10 event type 0x%x at %lu in binlog file '%s'", hdr->event_type, pos, file->binlogname);
			return NULL;
		}
	} else {
		if (hdr->event_type > MAX_EVENT_TYPE) {
			snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Invalid event type 0x%x at %lu in binlog file '%s'", hdr->event_type, pos, file->binlogname);
			return NULL;
		} 
	} 

	if (hdr->next_pos < pos && hdr->event_type != ROTATE_EVENT)
	{
		MXS_ERROR("Next position in header appears to be incorrect "
                          "rereading event header at pos %lu in file %s, "
                          "file size is %lu. Master will write %lu in %s next.",
                          pos, file->binlogname, filelen, router->binlog_position,
                          router->binlog_name);

		if ((n = pread(file->fd, hdbuf, BINLOG_EVENT_HDR_LEN, pos)) != BINLOG_EVENT_HDR_LEN)
		{
			switch (n)
			{
			case 0:
				MXS_DEBUG("Reached end of binlog file at %lu.",
                                          pos);

				/* set ok indicator */
				hdr->ok = SLAVE_POS_READ_OK;

				break;
			case -1:
				{
				char err_msg[STRERROR_BUFLEN];
				snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Failed to reread header in binlog file '%s'; (%s), event at %lu",
					file->binlogname, strerror_r(errno, err_msg, sizeof(err_msg)), pos);

				if (errno == EBADF)
					snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Bad file descriptor rereading header for binlog file '%s', "
						"refcount %d, descriptor %d, event at %lu",
						file->binlogname, file->refcnt, file->fd, pos);
				break;
				}
			default:
				snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Bogus data rereading log event header; "
					"expected %d bytes but read %d, position %lu in binlog file '%s'",
					 BINLOG_EVENT_HDR_LEN, n, pos, file->binlogname);
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
			snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Next event position still incorrect after rereading, "
				"event at %lu in binlog file '%s'", pos, file->binlogname);
			return NULL;
		}
		else
		{
			MXS_ERROR("Next position corrected by "
                                  "rereading");
		}
	}
	if ((result = gwbuf_alloc(hdr->event_size)) == NULL)
	{
		snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Failed to allocate memory for binlog entry, size %d, event at %lu in binlog file '%s'",
			hdr->event_size, pos, file->binlogname);
		return NULL;
	}

	data = GWBUF_DATA(result);

	memcpy(data, hdbuf, BINLOG_EVENT_HDR_LEN);	// Copy the header in

	if ((n = pread(file->fd, &data[BINLOG_EVENT_HDR_LEN], hdr->event_size - BINLOG_EVENT_HDR_LEN, pos + BINLOG_EVENT_HDR_LEN))
			!= hdr->event_size - BINLOG_EVENT_HDR_LEN)	// Read the balance
	{
		if (n == -1)
		{
			char err_msg[STRERROR_BUFLEN];
			snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Error reading the binlog event at %lu in binlog file '%s';"
				"(%s), expected %d bytes.",
				pos, file->binlogname, strerror_r(errno, err_msg, sizeof(err_msg)), hdr->event_size - BINLOG_EVENT_HDR_LEN);	
		}
		else
		{
			snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Bogus data in log event entry; "
				"expected %d bytes but got %d, position %lu in binlog file '%s'",
				 hdr->event_size - BINLOG_EVENT_HDR_LEN, n, pos, file->binlogname);

			if (filelen != 0 && filelen - pos < hdr->event_size)
			{
				snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Binlog event is close to the end of the binlog file; "
					"current file size is %lu, event at %lu in binlog file '%s'",
					filelen, pos, file->binlogname);
			}
			blr_log_header(LOG_ERR, "Possible malformed event header", hdbuf);
		}

		gwbuf_free(result);

		return NULL;
	}

	/* set OK indicator */
	hdr->ok = SLAVE_POS_READ_OK;

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
	spinlock_acquire(&router->fileslock);
	file->refcnt--;
	if (file->refcnt == 0)
	{
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
	}
	else
	{
		file = NULL;
	}
	spinlock_release(&router->fileslock);

	if (file)
	{
		close(file->fd);
		file->fd = -1;
		free(file);
	}
}

/**
 * Log the event header of  binlog event
 *
 * @param	priority The syslog priority of the message (LOG_ERR, LOG_WARNING, etc.)
 * @param 	msg	 A message strign to preceed the header with
 * @param	ptr	 The event header raw data
 */
static void
blr_log_header(int priority, char *msg, uint8_t *ptr)
{
char	buf[400], *bufp;
int	i;

	bufp = buf;
	bufp += sprintf(bufp, "%s: ", msg);
	for (i = 0; i < BINLOG_EVENT_HDR_LEN; i++)
		bufp += sprintf(bufp, "0x%02x ", ptr[i]);
	MXS_LOG_MESSAGE(priority, "%s", buf);
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


/**
 * Write the response packet to a cache file so that MaxScale can respond
 * even if there is no master running when MaxScale starts.
 *
 * cache dir is 'cache' under router->binlogdir
 *
 * @param router	The instance of the router
 * @param response	The name of the response, used to name the cached file
 * @param buf		The buffer to written to the cache
 */
void
blr_cache_response(ROUTER_INSTANCE *router, char *response, GWBUF *buf)
{
char	path[PATH_MAX+1] = "";
char	 *ptr;
int	fd;

	strncpy(path, router->binlogdir, PATH_MAX);
	strncat(path, "/cache", PATH_MAX);

	if (access(path, R_OK) == -1) {
		mkdir(path, 0700);
	}

	strncat(path, "/", PATH_MAX);
	strncat(path, response, PATH_MAX);

	if ((fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1)
		return;
	write(fd, GWBUF_DATA(buf), GWBUF_LENGTH(buf));

	close(fd);
}

/**
 * Read a cached copy of a master response message. This allows
 * the router to start and serve any binlogs it already has on disk
 * if the master is not available.
 *
 * cache dir is 'cache' under router->binlogdir
 *
 * @param router	The router instance structure
 * @param response	The name of the response
 * @return A pointer to a GWBUF structure
 */
GWBUF *
blr_cache_read_response(ROUTER_INSTANCE *router, char *response)
{
struct	stat	statb;
char	path[PATH_MAX+1] = "";
char	*ptr;
int	fd;
GWBUF	*buf;

	strncpy(path, router->binlogdir, PATH_MAX);
	strncat(path, "/cache", PATH_MAX);
	strncat(path, "/", PATH_MAX);
	strncat(path, response, PATH_MAX);

	if ((fd = open(path, O_RDONLY)) == -1)
		return NULL;

	if (fstat(fd, &statb) != 0)
	{
		close(fd);
		return NULL;
	}
	if ((buf = gwbuf_alloc(statb.st_size)) == NULL)
	{
		close(fd);
		return NULL;
	}
	read(fd, GWBUF_DATA(buf), statb.st_size);
	close(fd);
	return buf;
}

/**
 * Does the next binlog file in the sequence for the slave exist.
 *
 * @param router	The router instance
 * @param slave		The slave in question
 * @retuen 		0 if the next file does not exist
 */
int
blr_file_next_exists(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave)
{
char	*sptr, buf[BLRM_BINLOG_NAME_STR_LEN], bigbuf[PATH_MAX + 1];
int	filenum;

	if ((sptr = strrchr(slave->binlogfile, '.')) == NULL)
		return 0;
	filenum = atoi(sptr + 1);
	sprintf(buf, BINLOG_NAMEFMT, router->fileroot, filenum + 1);
	sprintf(bigbuf, "%s/%s", router->binlogdir, buf);
	if (access(bigbuf, R_OK) == -1)
		return 0;
	return 1;
}

/**
 * Read all replication events from a binlog file.
 *
 * Routine detects errors and pending transactions
 *
 * @param router        The router instance
 * @param fix           Whether to fix or not errors
 * @param debug         Whether to enable or not the debug for events
 * @return              0 on success, >0 on failure
 */
int
blr_read_events_all_events(ROUTER_INSTANCE *router, int fix, int debug) {
unsigned long   filelen = 0;
struct  stat    statb;
uint8_t         hdbuf[BINLOG_EVENT_HDR_LEN];
uint8_t         *data;
GWBUF           *result;
unsigned long long pos = 4;
unsigned long long last_known_commit = 4;

REP_HEADER hdr;
int pending_transaction = 0;
int n;
int db_name_len;
char *statement_sql;
uint8_t *ptr;
int len;
int var_block_len;
int statement_len;
int found_chksum = 0;
int event_error = 0;
unsigned long transaction_events = 0;
unsigned long total_events = 0;
unsigned long total_bytes = 0;
unsigned long n_transactions = 0;
unsigned long max_events = 0;
unsigned long event_bytes = 0;
unsigned long max_bytes = 0;
double average_events = 0;
double average_bytes = 0;
BINLOG_EVENT_DESC first_event;
BINLOG_EVENT_DESC last_event;
BINLOG_EVENT_DESC fde_event;
int fde_seen = 0;

	memset(&first_event, '\0', sizeof(first_event));
	memset(&last_event, '\0', sizeof(last_event));
	memset(&fde_event, '\0', sizeof(fde_event));

	if (router->binlog_fd == -1) {
		MXS_ERROR("Current binlog file %s is not open",
                          router->binlog_name);
                return 1;
        }

        if (fstat(router->binlog_fd, &statb) == 0)
                filelen = statb.st_size;

	router->current_pos = 4;
	router->binlog_position = 4;
	router->current_safe_event = 4;

        while (1){

                /* Read the header information from the file */
                if ((n = pread(router->binlog_fd, hdbuf, BINLOG_EVENT_HDR_LEN, pos)) != BINLOG_EVENT_HDR_LEN) {
                        switch (n)
                        {
                                case 0:
                                        MXS_DEBUG("End of binlog file [%s] at %llu.",
                                                  router->binlog_name,
                                                  pos);
					if (n_transactions)
						average_events = (double)((double)total_events / (double)n_transactions) * (1.0);
					if (n_transactions)
						average_bytes = (double)((double)total_bytes / (double)n_transactions) * (1.0);

					/* Report Binlog First and Last event */
					if (pos > 4) {
						if (first_event.event_type == 0)
							blr_print_binlog_details(router, fde_event, last_event);
						else
							blr_print_binlog_details(router, first_event, last_event);
					}

					/* Report Transaction Summary */
					if (n_transactions != 0) {
						char total_label[2]="";
						char average_label[2]="";
						char max_label[2]="";
						double format_total_bytes = total_bytes;
						double format_max_bytes = max_bytes;

						blr_format_event_size(&format_total_bytes, total_label);
						blr_format_event_size(&average_bytes, average_label);
						blr_format_event_size(&format_max_bytes, max_label);

                                        	MXS_NOTICE("Transaction Summary for binlog '%s'\n"
                                                           "\t\t\tDescription        %17s%17s%17s\n\t\t\t"
                                                           "No. of Transactions %16lu\n\t\t\t"
                                                           "No. of Events       %16lu %16.1f %16lu\n\t\t\t"
                                                           "No. of Bytes       %16.1f%s%16.1f%s%16.1f%s",
                                                           router->binlog_name,
                                                           "Total", "Average", "Max",
                                                           n_transactions, total_events,
                                                           average_events, max_events,
                                                           format_total_bytes, total_label,
                                                           average_bytes, average_label,
                                                           format_max_bytes, max_label);
					}

                                        if (pending_transaction) {
                                                MXS_WARNING("Binlog file %s contains a previous Opened "
                                                            "Transaction @ %llu. This pos is safe for slaves",
                                                            router->binlog_name,
                                                            last_known_commit);

                                        }

                                        break;
                                case -1:
					{
					char err_msg[BLRM_STRERROR_R_MSG_SIZE+1] = "";
					strerror_r(errno, err_msg, BLRM_STRERROR_R_MSG_SIZE);
                                        MXS_ERROR("Failed to read binlog file %s at position %llu"
                                                  " (%s).", router->binlog_name,
                                                  pos, err_msg);

                                        if (errno == EBADF)
                                                MXS_ERROR("Bad file descriptor in read binlog for file %s"
                                                          ", descriptor %d.",
                                                          router->binlog_name, router->binlog_fd);
                                        break;
					}
                                default:
                                        MXS_ERROR("Short read when reading the header. "
                                                  "Expected 19 bytes but got %d bytes. "
                                                  "Binlog file is %s, position %llu",
                                                  n, router->binlog_name, pos);
                                        break;
                        }

                        /**
			 * Check for errors and force last_known_commit position
			 * and current pos
			 */

                        if (pending_transaction) {
                                router->binlog_position = last_known_commit;
                                router->current_safe_event = last_known_commit;
				router->current_pos = pos;
                                router->pending_transaction = 1;
                                pending_transaction = 0;

				MXS_ERROR("Binlog '%s' ends at position %lu and has an incomplete transaction at %lu. ",
					router->binlog_name, router->current_pos, router->binlog_position);

				return 0;
                        } else {
                        	/* any error */
                        	if (n != 0) {
					router->binlog_position = last_known_commit;
					router->current_safe_event = last_known_commit;
					router->current_pos = pos;

					MXS_WARNING("an error has been found. "
                                                    "Setting safe pos to %lu, current pos %lu",
                                                    router->binlog_position, router->current_pos);
					if (fix) {
						if (ftruncate(router->binlog_fd, router->binlog_position) == 0) {
							MXS_NOTICE("Binlog file %s has been truncated at %lu",
                                                                   router->binlog_name,
                                                                   router->binlog_position);
							fsync(router->binlog_fd);
						}
					}
				
					return 1;
				} else {
					router->binlog_position = pos;
					router->current_safe_event = pos;
					router->current_pos = pos;

                                	return 0;
				}
                        }
                }

		/* fill replication header struct */
                hdr.timestamp = EXTRACT32(hdbuf);
                hdr.event_type = hdbuf[4];
                hdr.serverid = EXTRACT32(&hdbuf[5]);
                hdr.event_size = extract_field(&hdbuf[9], 32);
                hdr.next_pos = EXTRACT32(&hdbuf[13]);
                hdr.flags = EXTRACT16(&hdbuf[17]);

                /* Check event type against MAX_EVENT_TYPE */

		if (router->mariadb10_compat) {
			if (hdr.event_type > MAX_EVENT_TYPE_MARIADB10) {
				MXS_ERROR("Invalid MariaDB 10 event type 0x%x. "
                                          "Binlog file is %s, position %llu",
                                          hdr.event_type,
                                          router->binlog_name, pos);

				event_error = 1;
			}
		} else {
			if (hdr.event_type > MAX_EVENT_TYPE) {
				MXS_ERROR("Invalid event type 0x%x. "
                                          "Binlog file is %s, position %llu",
                                          hdr.event_type,
                                          router->binlog_name, pos);

				event_error = 1;
			}
		}

		if (event_error) {

			router->binlog_position = last_known_commit;
			router->current_safe_event = last_known_commit;
			router->current_pos = pos;

			MXS_WARNING("an error has been found in %s. "
                                    "Setting safe pos to %lu, current pos %lu",
                                    router->binlog_name,
                                    router->binlog_position,
                                    router->current_pos);

			if (fix) {
				if (ftruncate(router->binlog_fd, router->binlog_position) == 0) {
					MXS_NOTICE("Binlog file %s has been truncated at %lu",
                                                   router->binlog_name,
                                                   router->binlog_position);
					fsync(router->binlog_fd);
				}
			}

                        return 1;
		}

		if (hdr.event_size <= 0)
                {
                        MXS_ERROR("Event size error: "
                                  "size %d at %llu.",
                                  hdr.event_size, pos);

                        router->binlog_position = last_known_commit;
                        router->current_safe_event = last_known_commit;
                        router->current_pos = pos;

			MXS_WARNING("an error has been found. "
                                    "Setting safe pos to %lu, current pos %lu",
                                    router->binlog_position, router->current_pos);
			if (fix) {
				if (ftruncate(router->binlog_fd, router->binlog_position) == 0) {
					MXS_NOTICE("Binlog file %s has been truncated at %lu",
                                                   router->binlog_name,
                                                   router->binlog_position);
					fsync(router->binlog_fd);
				}
			}

                        return 1;
		}

                /* Allocate a GWBUF for the event */
                if ((result = gwbuf_alloc(hdr.event_size)) == NULL)
                {
                        MXS_ERROR("Failed to allocate memory for binlog entry, "
                                  "size %d at %llu.",
                                  hdr.event_size, pos);

                        router->binlog_position = last_known_commit;
                        router->current_safe_event = last_known_commit;
                        router->current_pos = pos;

			MXS_WARNING("an error has been found. "
                                    "Setting safe pos to %lu, current pos %lu",
                                    router->binlog_position, router->current_pos);

			if (fix) {
				if (ftruncate(router->binlog_fd, router->binlog_position) == 0) {
					MXS_NOTICE("Binlog file %s has been truncated at %lu",
                                                   router->binlog_name,
                                                   router->binlog_position);
					fsync(router->binlog_fd);
				}
			}

                        return 1;
                }

                /* Copy the header in the buffer */
                data = GWBUF_DATA(result);
                memcpy(data, hdbuf, BINLOG_EVENT_HDR_LEN);// Copy the header in

                /* Read event data */
                if ((n = pread(router->binlog_fd, &data[BINLOG_EVENT_HDR_LEN], hdr.event_size - BINLOG_EVENT_HDR_LEN, pos + BINLOG_EVENT_HDR_LEN)) != hdr.event_size - BINLOG_EVENT_HDR_LEN)
                {
                        if (n == -1)
                        {
				char err_msg[BLRM_STRERROR_R_MSG_SIZE+1] = "";
				strerror_r(errno, err_msg, BLRM_STRERROR_R_MSG_SIZE);
                                MXS_ERROR("Error reading the event at %llu in %s. "
                                          "%s, expected %d bytes.",
                                          pos, router->binlog_name,
                                          err_msg, hdr.event_size - BINLOG_EVENT_HDR_LEN);
                        }
                        else
                        {
                                MXS_ERROR("Short read when reading the event at %llu in %s. "
                                          "Expected %d bytes got %d bytes.",
                                          pos, router->binlog_name,
                                          hdr.event_size - BINLOG_EVENT_HDR_LEN, n);

                                if (filelen > 0 && filelen - pos < hdr.event_size)
                                {
                                        MXS_ERROR("Binlog event is close to the end of the binlog file %s, "
                                                  " size is %lu.",
                                                  router->binlog_name, filelen);
                                }
                        }

                        gwbuf_free(result);

                        router->binlog_position = last_known_commit;
                        router->current_safe_event = last_known_commit;
                        router->current_pos = pos;

			MXS_WARNING("an error has been found. "
                                    "Setting safe pos to %lu, current pos %lu",
                                    router->binlog_position, router->current_pos);
			if (fix) {
				if (ftruncate(router->binlog_fd, router->binlog_position) == 0) {
					MXS_NOTICE("Binlog file %s has been truncated at %lu",
                                                   router->binlog_name,
                                                   router->binlog_position);
					fsync(router->binlog_fd);
				}
			}

                        return 1;
                }

                /* check for pending transaction */
                if (pending_transaction == 0) {
                        last_known_commit = pos;
                }

		/* get firts event timestamp, after FDE */
		if (fde_seen) {
                        first_event.event_time = (unsigned long)hdr.timestamp;
			first_event.event_type = hdr.event_type;
			first_event.event_pos = pos;
			fde_seen = 0;
		}

                /* get event content */
                ptr = data+BINLOG_EVENT_HDR_LEN;

                /* check for FORMAT DESCRIPTION EVENT */
                if(hdr.event_type == FORMAT_DESCRIPTION_EVENT) {
                        int event_header_length;
                        int event_header_ntypes;
                        int n_events;
                        int check_alg;
                        uint8_t *checksum;
			char	buf_t[40];
			struct	tm	tm_t;

			fde_seen = 1;
			fde_event.event_time = (unsigned long)hdr.timestamp;
			fde_event.event_type = hdr.event_type;
			fde_event.event_pos = pos;

			localtime_r(&fde_event.event_time, &tm_t);
			asctime_r(&tm_t, buf_t);

			if (buf_t[strlen(buf_t)-1] == '\n') {
				buf_t[strlen(buf_t)-1] = '\0';
			}

                        if(debug)
                                MXS_DEBUG("- Format Description event FDE @ %llu, size %lu, time %lu (%s)",
                                          pos, (unsigned long)hdr.event_size, fde_event.event_time, buf_t);

                        event_header_length =  ptr[2 + 50 + 4];
                        event_header_ntypes = hdr.event_size - event_header_length - (2 + 50 + 4 + 1);

                        if (event_header_ntypes == 168) {
                                /* mariadb 10 LOG_EVENT_TYPES*/
                                event_header_ntypes -= 163;
                        } else {
                                if (event_header_ntypes == 165) {
                                        /* mariadb 5 LOG_EVENT_TYPES*/
                                        event_header_ntypes -= 160;
                                } else {
                                        /* mysql 5.6 LOG_EVENT_TYPES = 35 */
                                        event_header_ntypes -= 35;
                                }
                        }

                        n_events = hdr.event_size - event_header_length - (2 + 50 + 4 + 1);

                        if(debug) {
                                MXS_DEBUG("       FDE ServerVersion [%50s]", ptr + 2);

                                MXS_DEBUG("       FDE Header EventLength %i"
                                          ", N. of supported MySQL/MariaDB events %i",
                                          event_header_length,
                                          (n_events - event_header_ntypes));
                        }

                        if (event_header_ntypes < n_events) {
                                checksum = ptr + hdr.event_size - event_header_length - event_header_ntypes;
                                check_alg = checksum[0];

                                if(debug)
                                        MXS_DEBUG("       FDE Checksum alg desc %i, alg type %s",
                                                  check_alg,
                                                  check_alg == 1 ?
                                                  "BINLOG_CHECKSUM_ALG_CRC32" : "NONE or UNDEF");
                                if (check_alg == 1) {
                                        found_chksum = 1;
                                } else  {
                                        found_chksum = 0;
                                }
                        }
                }

		/* set last event time, pos and type */
		last_event.event_time = (unsigned long)hdr.timestamp;
		last_event.event_type = hdr.event_type;
		last_event.event_pos = pos;

                /* Decode ROTATE EVENT */
                if(hdr.event_type == ROTATE_EVENT) {
                        int             len, slen;
                        uint64_t        new_pos;
                        char            file[BINLOG_FNAMELEN+1];

                        len = hdr.event_size - BINLOG_EVENT_HDR_LEN;
                        new_pos = extract_field(ptr+4, 32);
                        new_pos <<= 32;
                        new_pos |= extract_field(ptr, 32);
                        slen = len - (8 + 4);           // Allow for position and CRC
                        if (found_chksum == 0)
                                slen += 4;
                        if (slen > BINLOG_FNAMELEN)
                                slen = BINLOG_FNAMELEN;
                        memcpy(file, ptr + 8, slen);
                        file[slen] = 0;

                        if(debug)
                                MXS_DEBUG("- Rotate event @ %llu, next file is [%s] @ %lu",
                                          pos, file, new_pos);
                }

		/* If MariaDB 10 compatibility:
		 * check for MARIADB10_GTID_EVENT with flags = 0
		 * This marks the transaction starts instead of
		 * QUERY_EVENT with "BEGIN"
		 */

		if (router->mariadb10_compat) {
			if (hdr.event_type == MARIADB10_GTID_EVENT) {
				uint64_t n_sequence;	/* 8 bytes */
				uint32_t domainid;	/* 4 bytes */
				unsigned int flags;	/* 1 byte */
				n_sequence = extract_field(ptr, 64);
				domainid = extract_field(ptr + 8, 32);
				flags = *(ptr + 8 + 4);

				if (flags == 0) {
					if (pending_transaction > 0) {
						MXS_ERROR("Transaction cannot be @ pos %llu: "
                                                          "Another MariaDB 10 transaction (GTID %u-%u-%lu)"
                                                          " was opened at %llu",
                                                          pos, domainid, hdr.serverid,
                                                          n_sequence, last_known_commit);

						gwbuf_free(result);

						break;
					} else {
						pending_transaction = 1;

						transaction_events = 0;
						event_bytes = 0;

						if (debug)
							MXS_DEBUG("> MariaDB 10 Transaction (GTID %u-%u-%lu)"
                                                                  " starts @ pos %llu",
                                                                  domainid, hdr.serverid, n_sequence, pos);
					}
				}
			}
		}

                /**
		 * Check QUERY_EVENT 
		 *
		 * Check for BEGIN ( ONLY for mysql 5.6, mariadb 5.5 )
		 * Check for COMMIT (not transactional engines)
		 */

                if(hdr.event_type == QUERY_EVENT) {
                        char *statement_sql;
                        db_name_len = ptr[4 + 4];
                        var_block_len = ptr[4 + 4 + 1 + 2];

                        statement_len = hdr.event_size - BINLOG_EVENT_HDR_LEN - (4+4+1+2+2+var_block_len+1+db_name_len);

                        statement_sql = calloc(1, statement_len+1);
                        strncpy(statement_sql, (char *)ptr+4+4+1+2+2+var_block_len+1+db_name_len, statement_len);

                        /* A transaction starts with this event */
                        if (strncmp(statement_sql, "BEGIN", 5) == 0) {
                                if (pending_transaction > 0) {
                                        MXS_ERROR("Transaction cannot be @ pos %llu: "
                                                  "Another transaction was opened at %llu",
                                                  pos, last_known_commit);

                                        free(statement_sql);
                                        gwbuf_free(result);

                                        break;
                                } else {
                                        pending_transaction = 1;

					transaction_events = 0;
					event_bytes = 0;

                                        if (debug)
                                                MXS_DEBUG("> Transaction starts @ pos %llu", pos);
                                }
                        }

                        /* Commit received for non transactional tables, i.e. MyISAM */
                        if (strncmp(statement_sql, "COMMIT", 6) == 0) {
                                if (pending_transaction > 0) {
                                        pending_transaction = 3;

                                if (debug)
                                        MXS_DEBUG("       Transaction @ pos %llu, closing @ %llu",
                                                  last_known_commit, pos);
                                }
                        }
                        free(statement_sql);

                }

                if(hdr.event_type == XID_EVENT) {
                        /* Commit received for a transactional tables, i.e. InnoDB */

                        if (pending_transaction > 0) {
                                pending_transaction = 2;
                                if (debug)
                                        MXS_DEBUG("       Transaction XID @ pos %llu, closing @ %llu",
                                                  last_known_commit, pos);
                        }
                }

                if (pending_transaction > 1) {
                        if (debug)
                                MXS_DEBUG("< Transaction @ pos %llu, is now closed @ %llu. %lu events seen",
                                          last_known_commit, pos, transaction_events);
                        pending_transaction = 0;
                        last_known_commit = pos;

			total_events += transaction_events;

			if (transaction_events > max_events)
				max_events = transaction_events;

			n_transactions++;
                }

                gwbuf_free(result);

                /* pos and next_pos sanity checks */
                if (hdr.next_pos > 0 && hdr.next_pos < pos) {
                        MXS_INFO("Binlog %s: next pos %u < pos %llu, truncating to %llu",
                                 router->binlog_name,
                                 hdr.next_pos,
                                 pos,
                                 pos);

                        router->binlog_position = last_known_commit;
                        router->current_safe_event = last_known_commit;
                        router->current_pos = pos;

			MXS_WARNING("an error has been found. "
                                    "Setting safe pos to %lu, current pos %lu",
                                    router->binlog_position, router->current_pos);
			if (fix) {
				if (ftruncate(router->binlog_fd, router->binlog_position) == 0) {
					MXS_NOTICE("Binlog file %s has been truncated at %lu",
                                                   router->binlog_name,
                                                   router->binlog_position);
					fsync(router->binlog_fd);
				}
			}

                        return 2;
                }

                if (hdr.next_pos > 0 && hdr.next_pos != (pos + hdr.event_size)) {
                        MXS_INFO("Binlog %s: next pos %u != (pos %llu + event_size %u), truncating to %llu",
                                 router->binlog_name,
                                 hdr.next_pos,
                                 pos,
                                 hdr.event_size,
                                 pos);

                        router->binlog_position = last_known_commit;
                        router->current_safe_event = last_known_commit;
                        router->current_pos = pos;

			MXS_WARNING("an error has been found. "
                                    "Setting safe pos to %lu, current pos %lu",
                                    router->binlog_position, router->current_pos);

			if (fix) {
				if (ftruncate(router->binlog_fd, router->binlog_position) == 0) {
					MXS_NOTICE("Binlog file %s has been truncated at %lu",
                                                   router->binlog_name,
                                                   router->binlog_position);
					fsync(router->binlog_fd);
				}
			}

                        return 2;
                }

                /* set pos to new value */
                if (hdr.next_pos > 0) {

			if (pending_transaction) {
				total_bytes += hdr.event_size;
				event_bytes += hdr.event_size;

				if (event_bytes > max_bytes)
					max_bytes = event_bytes;
			}

                        pos = hdr.next_pos;
                } else {

                        MXS_ERROR("Current event type %d @ %llu has nex pos = %u : exiting",
                                  hdr.event_type, pos, hdr.next_pos);
                        break;
                }

		transaction_events++;
        }

        if (pending_transaction) {
                MXS_INFO("Binlog %s contains an Open Transaction, truncating to %llu",
                         router->binlog_name,
                         last_known_commit);

		router->binlog_position = last_known_commit;
		router->current_safe_event = last_known_commit;
		router->current_pos = pos;
		router->pending_transaction = 1;

		MXS_WARNING("an error has been found. "
                            "Setting safe pos to %lu, current pos %lu",
                            router->binlog_position, router->current_pos);

                return 0;
        } else {
                router->binlog_position = pos;
                router->current_safe_event = pos;
                router->current_pos = pos;

                return 0;
        }
}

/**
 * Format a number to G, M, k, or B size
 *
 * @param event_size	The number to format
 * @param label		Label to use for display the formattted number
 */
static void
blr_format_event_size(double *event_size, char *label)
{
	if (*event_size > (1024 * 1024 * 1024)) {
		*event_size = *event_size / (1024 * 1024 * 1024);
		label[0] = 'G';
	} else if (*event_size > (1024 * 1024)) {
		*event_size = *event_size / (1024 * 1024);
		label[0] = 'M';
	} else if (*event_size > 1024) {
		*event_size = *event_size / (1024);
		label[0] = 'k';
	} else
		label[0] = 'B';
}

/**
 * Read any previously saved master data
 *
 * @param       router          The router instance
 */
void
blr_cache_read_master_data(ROUTER_INSTANCE *router)
{
	router->saved_master.server_id = blr_cache_read_response(router, "serverid");
	router->saved_master.heartbeat = blr_cache_read_response(router, "heartbeat");
	router->saved_master.chksum1 = blr_cache_read_response(router, "chksum1");
	router->saved_master.chksum2 = blr_cache_read_response(router, "chksum2");
	router->saved_master.gtid_mode = blr_cache_read_response(router, "gtidmode");
	router->saved_master.uuid = blr_cache_read_response(router, "uuid");
	router->saved_master.setslaveuuid = blr_cache_read_response(router, "ssuuid");
	router->saved_master.setnames = blr_cache_read_response(router, "setnames");
	router->saved_master.utf8 = blr_cache_read_response(router, "utf8");
	router->saved_master.select1 = blr_cache_read_response(router, "select1");
	router->saved_master.selectver = blr_cache_read_response(router, "selectver");
	router->saved_master.selectvercom = blr_cache_read_response(router, "selectvercom");
	router->saved_master.selecthostname = blr_cache_read_response(router, "selecthostname");
	router->saved_master.map = blr_cache_read_response(router, "map");
	router->saved_master.mariadb10 = blr_cache_read_response(router, "mariadb10");
}

/**
 * Get the next binlog file name.
 *
 * @param router	The router instance
 * @return 		0 on error, >0 as sequence number
 */
int
blr_file_get_next_binlogname(ROUTER_INSTANCE *router)
{
char	*sptr;
int	filenum;

	if ((sptr = strrchr(router->binlog_name, '.')) == NULL)
		return 0;
	filenum = atoi(sptr+1);
	if (filenum)
		filenum++;

	return filenum;
}

/**
 * Create a new binlog file
 *
 * @param router	The router instance
 * @param file		The new binlog file
 * @return		1 on success, 0 on failure
 */
int
blr_file_new_binlog(ROUTER_INSTANCE *router, char *file)
{
        return blr_file_create(router, file);
}

/**
 * Use current binlog file
 * @param router	The router instance
 * @param file		The binlog file
 */
void
blr_file_use_binlog(ROUTER_INSTANCE *router, char *file)
{
	return blr_file_append(router, file);
}

/**
 * Write a new ini file with master configuration
 *
 * File is 'inst->binlogdir/master.ini.tmp'
 * When done it's renamed to 'inst->binlogdir/master.ini'
 *
 * @param router	The current router instance
 * @param error		Preallocated error message
 * @return		0 on success, >0 on failure
 *
 */
int
blr_file_write_master_config(ROUTER_INSTANCE *router, char *error) {
char *section = "binlog_configuration";
FILE *config_file;
int rc;
char path[(PATH_MAX - 15) + 1] = "";
char filename[(PATH_MAX - 4) + 1] = "";
char tmp_file[PATH_MAX + 1] = "";
char err_msg[STRERROR_BUFLEN];

	strncpy(path, router->binlogdir, (PATH_MAX - 15));

	snprintf(filename,(PATH_MAX - 4), "%s/master.ini", path);

	snprintf(tmp_file, (PATH_MAX - 4), "%s", filename);

	strcat(tmp_file, ".tmp");

	/* open file for writing */
	config_file = fopen(tmp_file,"wb");
	if (config_file == NULL) {
		snprintf(error, BINLOG_ERROR_MSG_LEN, "%s, errno %u", strerror_r(errno, err_msg, sizeof(err_msg)), errno);
		return 2;
	}

	if(chmod(tmp_file, S_IRUSR | S_IWUSR) < 0) {
		snprintf(error, BINLOG_ERROR_MSG_LEN, "%s, errno %u", strerror_r(errno, err_msg, sizeof(err_msg)), errno);
        fclose(config_file);
		return 2;
	}

	/* write ini file section */
	fprintf(config_file,"[%s]\n", section);

	/* write ini file key=value */
	fprintf(config_file,"master_host=%s\n", router->service->dbref->server->name);
	fprintf(config_file,"master_port=%d\n", router->service->dbref->server->port);
	fprintf(config_file,"master_user=%s\n", router->user);
	fprintf(config_file,"master_password=%s\n", router->password);
	fprintf(config_file,"filestem=%s\n", router->fileroot);

	fclose(config_file);

	/* rename tmp file to right filename */
	rc = rename(tmp_file, filename);

	if (rc == -1) {
		snprintf(error, BINLOG_ERROR_MSG_LEN, "%s, errno %u", strerror_r(errno, err_msg, sizeof(err_msg)), errno);
		return 3;
	}

	if(chmod(filename, S_IRUSR | S_IWUSR) < 0) {
		snprintf(error, BINLOG_ERROR_MSG_LEN, "%s, errno %u", strerror_r(errno, err_msg, sizeof(err_msg)), errno);
		return 3;
	}

	return 0;
}

/** Print Binlog Details
 *
 * @param router	The router instance
 * @param first_event	First Event details
 * @param last_event	First Event details
 */

static void
blr_print_binlog_details(ROUTER_INSTANCE *router, BINLOG_EVENT_DESC first_event, BINLOG_EVENT_DESC last_event)
{
char    buf_t[40];
struct  tm      tm_t;
char    *event_desc;

	/* First Event */
	localtime_r(&first_event.event_time, &tm_t);
	asctime_r(&tm_t, buf_t);

	if (buf_t[strlen(buf_t)-1] == '\n') {
		buf_t[strlen(buf_t)-1] = '\0';
	}

	event_desc = blr_get_event_description(router, first_event.event_type);

	MXS_NOTICE("%lu @ %llu, %s, (%s), First EventTime",
                   first_event.event_time, first_event.event_pos,
                   event_desc != NULL ? event_desc : "unknown", buf_t);

	/* Last Event */
	localtime_r(&last_event.event_time, &tm_t);
	asctime_r(&tm_t, buf_t);

	if (buf_t[strlen(buf_t)-1] == '\n') {
		buf_t[strlen(buf_t)-1] = '\0';
	}

	event_desc = blr_get_event_description(router, last_event.event_type);

	MXS_NOTICE("%lu @ %llu, %s, (%s), Last EventTime",
                   last_event.event_time, last_event.event_pos,
                   event_desc != NULL ? event_desc : "unknown", buf_t);
}


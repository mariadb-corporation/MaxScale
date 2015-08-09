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
 * Date		Who			Description
 * 14/04/2014	Mark Riddoch		Initial implementation
 * 07/05/2015	Massimiliano Pinto	Added MAX_EVENT_TYPE_MARIADB10
 * 08/06/2015	Massimiliano Pinto	Addition of blr_cache_read_master_data()
 * 15/06/2015	Massimiliano Pinto	Addition of blr_file_get_next_binlogname()
 * 23/06/2015	Massimiliano Pinto	Addition of blr_file_use_binlog, blr_file_create_binlog
 * 29/06/2015	Massimiliano Pinto	Addition of blr_file_write_master_config()
 *					Cache directory is now 'cache' under router->binlogdir
 * 05/08/2015	Massimiliano Pinto	Initial implementation of transaction safety
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

extern int lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;


static int  blr_file_create(ROUTER_INSTANCE *router, char *file);
static void blr_file_append(ROUTER_INSTANCE *router, char *file);
static void blr_log_header(logfile_id_t file, char *msg, uint8_t *ptr);
void blr_cache_read_master_data(ROUTER_INSTANCE *router);
int blr_file_get_next_binlogname(ROUTER_INSTANCE *router);
int blr_file_new_binlog(ROUTER_INSTANCE *router, char *file);
void blr_file_use_binlog(ROUTER_INSTANCE *router, char *file);
int blr_file_write_master_config(ROUTER_INSTANCE *router, char *error);

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
char		*ptr, path[PATH_MAX+1], filename[PATH_MAX+1];
int		file_found, n = 1;
int		root_len, i;
DIR		*dirp;
struct dirent	*dp;

	if (router->binlogdir == NULL)
	{
		strcpy(path, get_datadir());
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
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"%s: Unable to read the binlog directory %s.",
					router->service->name, router->binlogdir)));
		return 0;
	}

	/* First try to find a binlog file number by reading the directory */
	root_len = strlen(router->fileroot);
	if ((dirp = opendir(path)) == NULL)
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"%s: Unable to read the binlog directory %s, %s.",
				router->service->name, router->binlogdir,
				strerror(errno))));
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
char		path[1024];
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
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"%s: Failed to create binlog file %s, %s.",
				router->service->name, path, strerror(errno))));
		return 0;
	}
	fsync(fd);
	close(router->binlog_fd);
	spinlock_acquire(&router->binlog_lock);
	strncpy(router->binlog_name, file,BINLOG_FNAMELEN);
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
	strncpy(router->binlog_name, file,BINLOG_FNAMELEN);
	router->current_pos = lseek(fd, 0L, SEEK_END);
	if (router->current_pos < 4) {
		if (router->current_pos == 0) {
			blr_file_add_magic(router, fd);
		} else {
			/* If for any reason the file's length is between 1 and 3 bytes
			 * then report an error. */
	                LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
				"%s: binlog file %s has an invalid length %d.",
				router->service->name, path, router->current_pos)));
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
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"%s: Failed to write binlog record at %d of %s, %s. "
			"Truncating to previous record.",
			router->service->name, hdr->next_pos - hdr->event_size,
			router->binlog_name,
			strerror(errno))));
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
char		path[1025];
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
	strncpy(file->binlogname, binlog,BINLOG_FNAMELEN+1);
	file->refcnt = 1;
	file->cache = 0;
	spinlock_init(&file->lock);

	strncpy(path, router->binlogdir,1024);
	strncat(path, "/",1024);
	strncat(path, binlog,1024);

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

	if (!file)
	{
		return NULL;
	}
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
				n, file->binlogname, pos)));
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

	if (router->mariadb10_compat) {
		if (hdr->event_type > MAX_EVENT_TYPE_MARIADB10) {
			LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
				"Invalid MariaDB 10 event type 0x%x. "
				"Binlog file is %s, position %d",
				hdr->event_type,
				file->binlogname, pos)));
			return NULL;
		}
	} else {
		if (hdr->event_type > MAX_EVENT_TYPE) {
			LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
				"Invalid event type 0x%x. " 
				"Binlog file is %s, position %d",
				hdr->event_type,
				file->binlogname, pos))); 

			return NULL;
		} 
	} 

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
char	path[PATH_MAX+1], *ptr;
int	fd;

	strncpy(path, router->binlogdir, PATH_MAX);
	strncat(path, "/cache", 4096);

	if (access(path, R_OK) == -1) {
		int mkdir_ret;
		mkdir_ret = mkdir(path, 0700);
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
char	path[PATH_MAX+1], *ptr;
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
char	*sptr, buf[80], bigbuf[4096];
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
char	*sptr, buf[80], bigbuf[4096];
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
char *master_host;
int master_port;
char *filestem;
char *master_user;
char *master_password;
char *section = "binlog_configuration";
FILE *config_file;
int rc;
char path[(PATH_MAX - 15) + 1] = "";
char filename[(PATH_MAX - 4) + 1] = "";
char tmp_file[PATH_MAX + 1] = "";

	strncpy(path, router->binlogdir, (PATH_MAX - 15));

	snprintf(filename,(PATH_MAX - 4), "%s/master.ini", path);

	snprintf(tmp_file, (PATH_MAX -4), filename);

	strcat(tmp_file, ".tmp");

	/* get data from current configuration */
	master_host = router->service->dbref->server->name;
	master_port = router->service->dbref->server->port;
	master_user = router->user;
	master_password= router->password;
	filestem = router->fileroot;

	/* open file for writing */
	config_file = fopen(tmp_file,"wb");
	if (config_file == NULL) {
		snprintf(error, BINLOG_ERROR_MSG_LEN, "%s, errno %u", strerror(errno), errno);
		return 2;
	}

	if(chmod(tmp_file, S_IRUSR | S_IWUSR) < 0) {
		snprintf(error, BINLOG_ERROR_MSG_LEN, "%s, errno %u", strerror(errno), errno);
		return 2;
	}

	/* write ini file section */
	fprintf(config_file,"[%s]\n", section);

	/* write ini file key=value */
	fprintf(config_file,"master_host=%s\n", master_host);
	fprintf(config_file,"master_port=%d\n", master_port);
	fprintf(config_file,"master_user=%s\n", master_user);
	fprintf(config_file,"master_password=%s\n", master_password);
	fprintf(config_file,"filestem=%s\n", filestem);

	fclose(config_file);

	/* rename tmp file to right filename */
	rc = rename(tmp_file, filename);

	if (rc == -1) {
		snprintf(error, BINLOG_ERROR_MSG_LEN, "%s, errno %u", strerror(errno), errno);
		return 3;
	}

	if(chmod(filename, S_IRUSR | S_IWUSR) < 0) {
		snprintf(error, BINLOG_ERROR_MSG_LEN, "%s, errno %u", strerror(errno), errno);
		return 3;
	}

	return 0;
}

/**
 * Read all replication events from a binlog file in order to detect pending transactions
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
uint8_t         hdbuf[19];
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
int checksum_len=0;
int found_chksum = 0;

	if (router->binlog_fd == -1) {
		LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
                        "*** ERROR: Current binlog file %s is not open",
                        router->binlog_name)));
                return 1;
        }

        if (fstat(router->binlog_fd, &statb) == 0)
                filelen = statb.st_size;

	router->current_pos = 4;
	router->binlog_position = 4;

        while (1){

                /* Read the header information from the file */
                if ((n = pread(router->binlog_fd, hdbuf, 19, pos)) != 19) {
                        switch (n)
                        {
                                case 0:
                                        LOGIF(LD, (skygw_log_write_flush(LOGFILE_DEBUG,
                                                "End of binlog file [%s] at %llu.",
                                                router->binlog_name,
                                                pos)));

                                        if (pending_transaction) {
                                                LOGIF(LT, (skygw_log_write_flush(LOGFILE_TRACE,
                                                        "WARNING: Binlog file %s contains a previous Opened Transaction, "
                                                        "Not Truncate file @ %llu but pos for slave is safe",
                                                        router->binlog_name,
                                                        last_known_commit)));

                                        }

                                        break;
                                case -1:
                                        LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
                                                "*** ERROR: Failed to read binlog file %s at position %llu"
                                                " (%s).", router->binlog_name,
                                                pos, strerror(errno))));
                                        if (errno == EBADF)
                                                LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
                                                        "*** ERROR: Bad file descriptor in read binlog for file %s"
                                                        ", descriptor %d.",
                                                        router->binlog_name, router->binlog_fd)));
                                        break;
                                default:
                                        LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
                                                "*** ERROR: Short read when reading the header. "
                                                "Expected 19 bytes but got %d bytes. "
                                                "Binlog file is %s, position %llu",
                                                n, router->binlog_name, pos)));
                                        break;
                        }

                        /**
			 * Check for errors and force last_known_commit position
			 * and current pos
			 */

                        if (pending_transaction) {
                                router->binlog_position = last_known_commit;
				router->current_pos = pos;
                                router->pending_transaction = 1;
                                pending_transaction = 0;

				LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
					"warning : pending transaction has been found. "
					"Setting safe pos to %lu, current pos %lu",
					router->binlog_position, router->current_pos)));

				return 0;
                        } else {
                        	/* any error */
                        	if (n != 0) {
					router->binlog_position = last_known_commit;
					router->current_pos = pos;

					LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
						"warning : an error has been found. "
						"Setting safe pos to %lu, current pos %lu",
						router->binlog_position, router->current_pos)));
					if (fix) {
						ftruncate(router->binlog_fd, router->binlog_position);
						fsync(router->binlog_fd);
					}
				
					return 1;
				} else {
					router->binlog_position = pos;
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

                //fprintf(stderr, ">>>> pos [%llu] event type %i\n", pos, hdr.event_type);
                //fprintf(stderr, ">>>> pos [%llu] event size %lu\n", pos, hdr.event_size);
                //fprintf(stderr, ">>>> pos [%llu] event next_pos %lu\n", pos, hdr.next_pos);

		/* TO DO */

		/* Add MariaDB 10 check */

                /* Check event type against MAX_EVENT_TYPE */
                if (hdr.event_type > MAX_EVENT_TYPE)
                {
                        LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
                                "*** ERROR: Found an Invalid event type 0x%x. "
                                "Binlog file is %s, position %llu",
                                hdr.event_type,
                                router->binlog_name, pos)));

                        router->binlog_position = last_known_commit;
                        router->current_pos = pos;

			LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
				"warning : an error has been found. "
				"Setting safe pos to %lu, current pos %lu",
				router->binlog_position, router->current_pos)));

			if (fix) {
				ftruncate(router->binlog_fd, router->binlog_position);
				fsync(router->binlog_fd);
			}

                        return 1;
                }

		if (hdr.event_size <= 0)
                {
                        LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
                                "*** ERROR: event size error: "
                                "size %d at %llu.",
                                hdr.event_size, pos)));

                        router->binlog_position = last_known_commit;
                        router->current_pos = pos;

			LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
				"warning : an error has been found. "
				"Setting safe pos to %lu, current pos %lu",
				router->binlog_position, router->current_pos)));
			if (fix) {
				ftruncate(router->binlog_fd, router->binlog_position);
				fsync(router->binlog_fd);
			}

                        return 1;
		}

                /* Allocate a GWBUF for the event */
                if ((result = gwbuf_alloc(hdr.event_size)) == NULL)
                {
                        LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
                                "*** ERROR: Failed to allocate memory for binlog entry, "
                                "size %d at %llu.",
                                hdr.event_size, pos)));

                        router->binlog_position = last_known_commit;
                        router->current_pos = pos;

			LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
				"warning : an error has been found. "
				"Setting safe pos to %lu, current pos %lu",
				router->binlog_position, router->current_pos)));

			if (fix) {
				ftruncate(router->binlog_fd, router->binlog_position);
				fsync(router->binlog_fd);
			}

                        return 1;
                }

                /* Copy the header in the buffer */
                data = GWBUF_DATA(result);
                memcpy(data, hdbuf, 19);// Copy the header in

                /* Read event data */
                if ((n = pread(router->binlog_fd, &data[19], hdr.event_size - 19, pos + 19)) != hdr.event_size - 19)
                {
                        if (n == -1)
                        {
                                LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
                                        "*** ERROR: Error reading the event at %llu in %s. "
                                        "%s, expected %d bytes.",
                                        pos, router->binlog_name,
                                        strerror(errno), hdr.event_size - 19)));
                        }
                        else
                        {
                                LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
                                        "*** ERROR: Short read when reading the event at %llu in %s. "
                                        "Expected %d bytes got %d bytes.",
                                        pos, router->binlog_name, hdr.event_size - 19, n)));

                                if (filelen > 0 && filelen - pos < hdr.event_size)
                                {
                                        LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
                                                "*** ERROR: Binlog event is close to the end of the binlog file %s, "
                                                " size is %lu.",
                                                router->binlog_name, filelen)));
                                }
                        }

                        gwbuf_free(result);

                        router->binlog_position = last_known_commit;
                        router->current_pos = pos;

			LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
				"warning : an error has been found. "
				"Setting safe pos to %lu, current pos %lu",
				router->binlog_position, router->current_pos)));
			if (fix) {
				ftruncate(router->binlog_fd, router->binlog_position);
				fsync(router->binlog_fd);
			}

                        return 1;
                }

                /* check for pending transaction */
                if (pending_transaction == 0) {
                        last_known_commit = pos;
                }

                /* get event content */
                ptr = data+19;

                /* check for FORMAT DESCRIPTION EVENT */
                if(hdr.event_type == FORMAT_DESCRIPTION_EVENT) {
                        int event_header_length;
                        int event_header_ntypes;
                        int n_events;
                        int check_alg;
                        uint8_t *checksum;

                        if(debug)
                                LOGIF(LD, (skygw_log_write_flush(LOGFILE_DEBUG,
                                        "- Format Description event FDE @ %llu, size %lu",
                                        pos, hdr.event_size)));

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
                                LOGIF(LD, (skygw_log_write_flush(LOGFILE_DEBUG,
                                        "       FDE ServerVersion [%50s]", ptr + 2)));

                                LOGIF(LD, (skygw_log_write_flush(LOGFILE_DEBUG,
                                        "       FDE Header EventLength %i"
                                        ", N. of supported MySQL/MAriaDB events %i",
                                        event_header_length,
                                        (n_events - event_header_ntypes))));
                        }

                        if (event_header_ntypes < n_events) {
                                checksum = ptr + hdr.event_size - event_header_length - event_header_ntypes;
                                check_alg = checksum[0];

                                if(debug)
                                        LOGIF(LD, (skygw_log_write_flush(LOGFILE_DEBUG,
                                                "       FDE Checksum alg desc %i, alg type %s",
                                                check_alg,
                                                check_alg == 1 ? "BINLOG_CHECKSUM_ALG_CRC32" : "NONE or UNDEF")));
                                if (check_alg == 1) {
                                        checksum_len = 4;
                                        found_chksum = 1;
                                } else  {
                                        found_chksum = 0;
                                }
                        }
                }
                /* Decode ROTATE EVENT */
                if(hdr.event_type == ROTATE_EVENT) {
                        int             len, slen;
                        uint64_t        new_pos;
                        char            file[BINLOG_FNAMELEN+1];

                        len = hdr.event_size - 19;
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
                                LOGIF(LD, (skygw_log_write_flush(LOGFILE_DEBUG,
                                        "- Rotate event @ %llu, next file is [%s] @ %llu",
                                        pos, file, new_pos)));
                }


                /* Check QUERY_EVENT */
                if(hdr.event_type == QUERY_EVENT) {
                        char *statement_sql;
                        db_name_len = ptr[4 + 4];
                        var_block_len = ptr[4 + 4 + 1 + 2];

                        statement_len = hdr.event_size - 19 - (4+4+1+2+2+var_block_len+1+db_name_len);
                        //if (checksum_len)
                        //              statement_len -= checksum_len;

                        statement_sql = calloc(1, statement_len+1);
                        strncpy(statement_sql, (char *)ptr+4+4+1+2+2+var_block_len+1+db_name_len, statement_len);

                        //fprintf(stderr, "QUERY_EVENT = [%s] %i / %i\n", statement_sql, (4+4+1+2+2+var_block_len+1+db_name_len), statement_len);

                        /* A transaction starts with this event */
                        if (strncmp(statement_sql, "BEGIN", 5) == 0) {
                                if (pending_transaction > 0) {
                                        LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
                                        "*** ERROR: Transaction cannot be @ pos %llu: "
                                        "Another transaction was opened at %ll",
                                        pos, last_known_commit)));

                                        free(statement_sql);
                                        gwbuf_free(result);

                                        break;
                                } else {
                                        pending_transaction = 1;

                                        if (debug)
                                                LOGIF(LD, (skygw_log_write_flush(LOGFILE_DEBUG,
                                                        "> Transaction starts @ pos %llu", pos)));
                                }
                        }

                        /* Commit received for non transactional tables, i.e. MyISAM */
                        if (strncmp(statement_sql, "COMMIT", 6) == 0) {
                                if (pending_transaction > 0) {
                                        pending_transaction = 3;

                                if (debug)
                                        LOGIF(LD, (skygw_log_write_flush(LOGFILE_DEBUG,
                                                "       Transaction @ pos %llu, closing @ %llu", last_known_commit, pos)));
                                }
                        }
                        free(statement_sql);

                }

                if(hdr.event_type == XID_EVENT) {
                        /* Commit received for a transactional tables, i.e. InnoDB */
                        uint64_t xid;
                        xid = extract_field(ptr, 64);

                        if (pending_transaction > 0) {
                                pending_transaction = 2;
                                if (debug)
                                        LOGIF(LD, (skygw_log_write_flush(LOGFILE_DEBUG,
                                                "       Transaction XID @ pos %llu, closing @ %llu", last_known_commit, pos)));
                        }
                }

                if (pending_transaction > 1) {
                        unsigned long long prev_pos = last_known_commit;
                        if (debug)
                                LOGIF(LD, (skygw_log_write_flush(LOGFILE_DEBUG,
                                        "< Transaction @ pos %llu, is now closed @ %llu", last_known_commit, pos)));
                        pending_transaction = 0;
                        last_known_commit = pos;
                }

                gwbuf_free(result);

                /* pos and next_pos sanity checks */
                if (hdr.next_pos > 0 && hdr.next_pos < pos) {
                        LOGIF(LT, (skygw_log_write_flush(LOGFILE_TRACE,
                                "Binlog %s: next pos %llu < pos %llu, truncating to %llu",
                                router->binlog_name,
                                hdr.next_pos,
                                pos,
                                pos)));

                        router->binlog_position = last_known_commit;
                        router->current_pos = pos;

			LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
				"warning : an error has been found. "
				"Setting safe pos to %lu, current pos %lu",
				router->binlog_position, router->current_pos)));
			if (fix) {
				ftruncate(router->binlog_fd, router->binlog_position);
				fsync(router->binlog_fd);
			}

                        return 2;
                }

                if (hdr.next_pos > 0 && hdr.next_pos != (pos + hdr.event_size)) {
                        LOGIF(LT, (skygw_log_write_flush(LOGFILE_TRACE,
                                "Binlog %s: next pos %llu != (pos %llu + event_size %llu), truncating to %llu",
                                router->binlog_name,
                                hdr.next_pos,
                                pos,
                                hdr.event_size,
                                pos)));

                        router->binlog_position = last_known_commit;
                        router->current_pos = pos;

			LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
				"warning : an error has been found. "
				"Setting safe pos to %lu, current pos %lu",
				router->binlog_position, router->current_pos)));

			if (fix) {
				ftruncate(router->binlog_fd, router->binlog_position);
				fsync(router->binlog_fd);
			}

                        return 2;
                }

                /* set pos to new value */
                if (hdr.next_pos > 0) {
                        pos = hdr.next_pos;
                } else {

                        LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
                                "*** ERROR: Current event type %lu @ %llu has nex pos = %llu : exiting", hdr.event_type, pos, hdr.next_pos)));
                        break;
                }
        }

        if (pending_transaction) {
                LOGIF(LT, (skygw_log_write_flush(LOGFILE_TRACE,
                        "Binlog %s contains an Open Transaction, truncating to %llu",
                        router->binlog_name,
                        last_known_commit)));

		router->binlog_position = last_known_commit;
		router->current_pos = pos;
		router->pending_transaction = 1;

		LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
			"warning : an error has been found. "
			"Setting safe pos to %lu, current pos %lu",
			router->binlog_position, router->current_pos)));

                return 0;
        } else {
                router->binlog_position = pos;
                router->current_pos = pos;

                return 0;
        }
}

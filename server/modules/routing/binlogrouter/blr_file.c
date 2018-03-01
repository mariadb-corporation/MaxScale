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
 * @file blr_file.c - contains code for the router binlog file management
 *
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                 Description
 * 14/04/2014   Mark Riddoch        Initial implementation
 * 07/05/2015   Massimiliano Pinto  Added MAX_EVENT_TYPE_MARIADB10
 * 08/06/2015   Massimiliano Pinto  Addition of blr_cache_read_master_data()
 * 15/06/2015   Massimiliano Pinto  Addition of blr_file_get_next_binlogname()
 * 23/06/2015   Massimiliano Pinto  Addition of blr_file_use_binlog, blr_file_create_binlog
 * 29/06/2015   Massimiliano Pinto  Addition of blr_file_write_master_config()
 *                                  Cache directory is now 'cache' under router->binlogdir
 * 05/08/2015   Massimiliano Pinto  Initial implementation of transaction safety
 * 24/08/2015   Massimiliano Pinto  Added strerror_r
 * 26/08/2015   Massimiliano Pinto  Added MariaDB 10 GTID event check with flags = 0
 *                                  This is the current supported condition for detecting
 *                                  MariaDB 10 transaction start point.
 *                                  It's no longer using QUERY_EVENT with BEGIN
 * 23/10/2015   Markus Makela       Added current_safe_event
 * 26/04/2016   Massimiliano Pinto  Added MariaDB 10.0 and 10.1 GTID event flags detection
 * 11/07/2016   Massimiliano Pinto  Added SSL backend support
 * 16/09/2016   Massimiliano Pinto  Addition of IGNORABLE_EVENT in case of a missing event
 *                                  detected from master binlog stream
 * 19/09/2016   Massimiliano Pinto  START_ENCRYPTION_EVENT is detected by maxbinlocheck.
 * 21/09/2016   Massimiliano Pinto  Addition of START_ENCRYPTION_EVENT: new event is written
 * 25/11/2016   Massimiliano Pinto  Binlog files can be encrypted with specified AES key
 *                                  and AES algorithm (Only AES_CTR right now).
 *                                  Events are decrypted before being sent to slaves.
 *                                  Events larger than 16MBytes are currently not suitable
 *                                  for ecryption/decryption.
 * 29/11/2016   Massimiliano Pinto  Binlog files can be encrypted with AES_CBC
 *
 * @endverbatim
 */

#include "blr.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <maxscale/service.h>
#include <maxscale/server.h>
#include <maxscale/router.h>
#include <maxscale/atomic.h>
#include <maxscale/spinlock.h>
#include <maxscale/dcb.h>
#include <maxscale/spinlock.h>
#include <maxscale/paths.h>
#include <maxscale/log_manager.h>
#include <maxscale/alloc.h>
#include <inttypes.h>
#include <maxscale/secrets.h>
#include <maxscale/encryption.h>

/**
 * AES_CTR handling
 *
 * @param klen    The AES Key len
 * @return        The EVP_AES_CTR routine for key len
 */

#if OPENSSL_VERSION_NUMBER > 0x10000000L
static inline const EVP_CIPHER *aes_ctr(unsigned int klen)
{
    switch (klen)
    {
    case 16:
        return EVP_aes_128_ctr();
    case 24:
        return EVP_aes_192_ctr();
    case 32:
        return EVP_aes_256_ctr();
    default:
        return 0;
    }
}
#endif

/**
 * AES_CBC handling
 *
 * @param klen    The AES Key len
 * @return        The EVP_AES_CBC routine for key len
 */
static inline const EVP_CIPHER *aes_cbc(uint klen)
{
    switch (klen)
    {
    case 16:
        return EVP_aes_128_cbc();
    case 24:
        return EVP_aes_192_cbc();
    case 32:
        return EVP_aes_256_cbc();
    default:
        return 0;
    }
}

/**
 * AES_ECB handling
 *
 * @param klen    The AES Key len
 * @return        The EVP_AES_ECB routine for key len
 */
static inline const EVP_CIPHER *aes_ecb(uint klen)
{
    switch (klen)
    {
    case 16:
        return EVP_aes_128_ecb();
    case 24:
        return EVP_aes_192_ecb();
    case 32:
        return EVP_aes_256_ecb();
    default:
        return 0;
    }
}

/**
 * Array of functions for supported algorithms
 */
const EVP_CIPHER *(*ciphers[])(unsigned int) =
{
    aes_cbc,
#if OPENSSL_VERSION_NUMBER > 0x10000000L
    aes_ctr,
#else
    NULL,
#endif
    aes_ecb
};

#if OPENSSL_VERSION_NUMBER > 0x10000000L
static const char *blr_encryption_algorithm_names[BINLOG_MAX_CRYPTO_SCHEME] = {"aes_cbc", "aes_ctr"};
static const char blr_encryption_algorithm_list_names[] = "aes_cbc, aes_ctr";
#else
static const char *blr_encryption_algorithm_names[BINLOG_MAX_CRYPTO_SCHEME] = {"aes_cbc"};
static const char blr_encryption_algorithm_list_names[] = "aes_cbc";
#endif

static int  blr_file_create(ROUTER_INSTANCE *router, char *file);
static void blr_log_header(int priority, char *msg, uint8_t *ptr);
void blr_cache_read_master_data(ROUTER_INSTANCE *router);
int blr_file_get_next_binlogname(ROUTER_INSTANCE *router);
int blr_file_new_binlog(ROUTER_INSTANCE *router, char *file);
int blr_file_write_master_config(ROUTER_INSTANCE *router, char *error);
extern uint32_t extract_field(uint8_t *src, int bits);
static void blr_format_event_size(double *event_size, char *label);
extern int MaxScaleUptime();
extern void encode_value(unsigned char *data, unsigned int value, int len);
extern void blr_extract_header(register uint8_t *ptr, register REP_HEADER *hdr);

typedef struct binlog_event_desc
{
    uint64_t event_pos;
    uint8_t event_type;
    time_t  event_time;
} BINLOG_EVENT_DESC;

static void blr_print_binlog_details(ROUTER_INSTANCE *router,
                                     BINLOG_EVENT_DESC first_event_time,
                                     BINLOG_EVENT_DESC last_event_time);
static uint8_t *blr_create_ignorable_event(uint32_t event_size,
                                           REP_HEADER *hdr,
                                           uint32_t event_pos,
                                           bool do_checksum);
static int blr_write_special_event(ROUTER_INSTANCE *router,
                                   uint32_t file_offset,
                                   uint32_t hole_size,
                                   REP_HEADER *hdr,
                                   int type);
static uint8_t *blr_create_start_encryption_event(ROUTER_INSTANCE *router,
                                                  uint32_t event_pos,
                                                  bool do_checksum);
static GWBUF *blr_prepare_encrypted_event(ROUTER_INSTANCE *router,
                                          uint8_t *event,
                                          uint32_t event_size,
                                          uint32_t pos,
                                          const uint8_t *nonce,
                                          int action);
static GWBUF *blr_aes_crypt(ROUTER_INSTANCE *router,
                            uint8_t *event,
                            uint32_t event_size,
                            uint8_t *iv,
                            int action);
static int blr_aes_create_tail_for_cbc(uint8_t *output,
                                       uint8_t *input,
                                       uint32_t in_size,
                                       uint8_t *iv,
                                       uint8_t *key,
                                       unsigned int key_len);
static int blr_binlog_event_check(ROUTER_INSTANCE *router,
                                  unsigned long pos,
                                  REP_HEADER *hdr,
                                  char *binlogname,
                                  char *errmsg);

static void blr_report_checksum(REP_HEADER hdr, const uint8_t *buffer, char *output);

/** MaxScale generated events */
typedef enum
{
    BLRM_IGNORABLE, /*< Ignorable event */
    BLRM_START_ENCRYPTION /*< Start Encryption event */
} generated_event_t;

/**
 * MariaDB 10.1.7 Start Encryption event content
 *
 * Event header:    19 bytes
 * Content size:    17 bytes
 *     crypto scheme 1 byte
 *     key_version   4 bytes
 *     nonce random 12 bytes
 *
 * Event size is 19 + 17 = 36 bytes
 */
typedef struct start_encryption_event
{
    uint8_t header[BINLOG_EVENT_HDR_LEN]; /**< Replication event header */
    uint8_t binlog_crypto_scheme; /**< Encryption scheme */
    uint32_t binlog_key_version;  /**< Encryption key version */
    uint8_t nonce[BLRM_NONCE_LENGTH]; /**< nonce (random bytes) of current binlog.
                                       * These bytes + the binlog event current pos
                                       * form the encrryption IV for the event */
} START_ENCRYPTION_EVENT;

/**
 * Initialise the binlog file for this instance. MaxScale will look
 * for all the binlogs that it has on local disk, determine the next
 * binlog to use and initialise it for writing, determining the
 * next record to be fetched from the real master.
 *
 * @param router    The router instance this defines the master for this replication chain
 */
int
blr_file_init(ROUTER_INSTANCE *router)
{
    char path[PATH_MAX + 1] = "";
    char filename[PATH_MAX + 1] = "";
    int file_found, n = 1;
    int root_len, i;
    DIR *dirp;
    struct dirent *dp;

    if (router->binlogdir == NULL)
    {
        const char* datadir = get_datadir();
        size_t len = strlen(datadir) + sizeof('/') + strlen(router->service->name);

        if (len > PATH_MAX)
        {
            MXS_ERROR("The length of %s/%s is more than the maximum length %d.",
                      datadir, router->service->name, PATH_MAX);
            return 0;
        }

        strcpy(path, datadir);
        strcat(path, "/");
        strcat(path, router->service->name);

        if (access(path, R_OK) == -1)
        {
            // TODO: Check what kind of error, ENOENT or something else.
            mkdir(path, 0700);
            // TODO: Check the result of mkdir.
        }

        router->binlogdir = MXS_STRDUP_A(path);
    }
    else
    {
        strcpy(path, router->binlogdir);
    }

    if (access(path, R_OK) == -1)
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
            {
                n = i;
            }
        }
    }
    closedir(dirp);


    file_found = 0;
    do
    {
        snprintf(filename, PATH_MAX, "%s/" BINLOG_NAMEFMT, path, router->fileroot, n);
        if (access(filename, R_OK) != -1)
        {
            file_found  = 1;
            n++;
        }
        else
        {
            file_found = 0;
        }
    }
    while (file_found);
    n--;

    if (n == 0)     // No binlog files found
    {
        snprintf(filename, PATH_MAX, BINLOG_NAMEFMT, router->fileroot, router->initbinlog);

        if (!blr_file_create(router, filename))
        {
            return 0;
        }
    }
    else
    {
        snprintf(filename, PATH_MAX, BINLOG_NAMEFMT, router->fileroot, n);
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
 * @param fd file descriptor to the open binlog file
 * @return   True if the magic string could be written to the file.
 */
static bool
blr_file_add_magic(int fd)
{
    static const unsigned char magic[] = BINLOG_MAGIC;

    ssize_t written = write(fd, magic, BINLOG_MAGIC_SIZE);

    return written == BINLOG_MAGIC_SIZE;
}


/**
 * Create a new binlog file for the router to use.
 *
 * @param router        The router instance
 * @param file          The binlog file name
 * @return              Non-zero if the fie creation succeeded
 */
static int
blr_file_create(ROUTER_INSTANCE *router, char *file)
{
    if (strlen(file) > BINLOG_FNAMELEN)
    {
        MXS_ERROR("The binlog filename %s is longer than the maximum allowed length %d.",
                  file, BINLOG_FNAMELEN);
        return 0;
    }

    int created = 0;
    char err_msg[MXS_STRERROR_BUFLEN];

    char path[PATH_MAX + 1] = "";

    strcpy(path, router->binlogdir);
    strcat(path, "/");
    strcat(path, file);

    int fd = open(path, O_RDWR | O_CREAT, 0666);

    if (fd != -1)
    {
        if (blr_file_add_magic(fd))
        {
            close(router->binlog_fd);
            spinlock_acquire(&router->binlog_lock);

            /// Use an intermediate buffer in case the source and destination overlap
            char new_binlog[strlen(file) + 1];
            strcpy(new_binlog, file);
            strcpy(router->binlog_name, new_binlog);

            router->binlog_fd = fd;
            router->current_pos = BINLOG_MAGIC_SIZE;     /* Initial position after the magic number */
            router->binlog_position = BINLOG_MAGIC_SIZE;
            router->current_safe_event = BINLOG_MAGIC_SIZE;
            router->last_written = BINLOG_MAGIC_SIZE;
            spinlock_release(&router->binlog_lock);

            created = 1;
        }
        else
        {
            MXS_ERROR("%s: Failed to write magic string to created binlog file %s, %s.",
                      router->service->name, path, strerror_r(errno, err_msg, sizeof(err_msg)));
            close(fd);

            if (!unlink(path))
            {
                MXS_ERROR("%s: Failed to delete file %s, %s.",
                          router->service->name, path, strerror_r(errno, err_msg, sizeof(err_msg)));
            }
        }
    }
    else
    {
        MXS_ERROR("%s: Failed to create binlog file %s, %s.",
                  router->service->name, path, strerror_r(errno, err_msg, sizeof(err_msg)));
    }

    return created;
}

/**
 * Prepare an existing binlog file to be appended to.
 *
 * @param router    The router instance
 * @param file      The binlog file name
 */
void
blr_file_append(ROUTER_INSTANCE *router, char *file)
{
    char path[PATH_MAX + 1] = "";
    int fd;

    strcpy(path, router->binlogdir);
    strcat(path, "/");
    strcat(path, file);

    if ((fd = open(path, O_RDWR | O_APPEND, 0666)) == -1)
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
    if (router->current_pos < 4)
    {
        if (router->current_pos == 0)
        {
            if (blr_file_add_magic(fd))
            {
                router->current_pos = BINLOG_MAGIC_SIZE;
                router->binlog_position = BINLOG_MAGIC_SIZE;
                router->current_safe_event = BINLOG_MAGIC_SIZE;
                router->last_written = BINLOG_MAGIC_SIZE;
            }
            else
            {
                MXS_ERROR("%s: Could not write magic to binlog file.", router->service->name);
            }
        }
        else
        {
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
 * @param router The router instance
 * @param buf    The binlog record
 * @param len    The length of the binlog record
 * @return       Return the number of bytes written
 */
int
blr_write_binlog_record(ROUTER_INSTANCE *router, REP_HEADER *hdr, uint32_t size, uint8_t *buf)
{
    int n = 0;
    bool write_start_encryption_event = false;
    uint64_t file_offset = router->current_pos;
    uint32_t event_size[4];

    /* Track whether FORMAT_DESCRIPTION_EVENT has been received */
    if (hdr->event_type == FORMAT_DESCRIPTION_EVENT)
    {
        write_start_encryption_event = true;
    }

    /**
     * Check first for possible hole looking at current pos and next pos
     * Fill the gap with a self generated ignorable event
     * Binlog file position is incremented by blr_write_special_event()
     */
    if (hdr->next_pos && (hdr->next_pos > (file_offset + size)))
    {
        uint64_t hole_size = hdr->next_pos - file_offset - size;
        if (!blr_write_special_event(router, file_offset, hole_size, hdr, BLRM_IGNORABLE))
        {
            return 0;
        }
        n = hole_size;
    }

    if (router->encryption.enabled && router->encryption_ctx != NULL)
    {
        GWBUF *encrypted;
        uint8_t *encr_ptr;
        if ((encrypted = blr_prepare_encrypted_event(router,
                                                     buf,
                                                     size,
                                                     router->current_pos,
                                                     NULL,
                                                     BINLOG_FLAG_ENCRYPT)) == NULL)
        {
            return 0;
        }

        encr_ptr = GWBUF_DATA(encrypted);

        n = pwrite(router->binlog_fd, encr_ptr, size, router->last_written);

        gwbuf_free(encrypted);
        encrypted = NULL;
    }
    else
    {
        /* Write current received event form master */
        n = pwrite(router->binlog_fd, buf, size, router->last_written);
    }

    /* Check write operation result*/
    if (n != size)
    {
        char err_msg[MXS_STRERROR_BUFLEN];
        MXS_ERROR("%s: Failed to write binlog record at %lu of %s, %s. "
                  "Truncating to previous record.",
                  router->service->name, router->binlog_position,
                  router->binlog_name,
                  strerror_r(errno, err_msg, sizeof(err_msg)));
        /* Remove any partial event that was written */
        if (ftruncate(router->binlog_fd, router->binlog_position))
        {
            MXS_ERROR("%s: Failed to truncate binlog record at %lu of %s, %s. ",
                      router->service->name, router->binlog_position,
                      router->binlog_name,
                      strerror_r(errno, err_msg, sizeof(err_msg)));
        }
        return 0;
    }

    /* Increment offsets */
    spinlock_acquire(&router->binlog_lock);
    router->current_pos = hdr->next_pos;
    router->last_written += size;
    router->last_event_pos = hdr->next_pos - hdr->event_size;
    spinlock_release(&router->binlog_lock);

    /* Check whether adding the Start Encryption event into current binlog */
    if (router->encryption.enabled && write_start_encryption_event)
    {
        uint64_t event_size = sizeof(START_ENCRYPTION_EVENT);
        uint64_t file_offset = router->current_pos;
        if (router->master_chksum)
        {
            event_size += BINLOG_EVENT_CRC_SIZE;
        }
        if (!blr_write_special_event(router, file_offset, event_size, hdr, BLRM_START_ENCRYPTION))
        {
            return 0;
        }
        /* At this point the router->encryption_ctx is set:
         * Encryption of new events can start
         */
        write_start_encryption_event = false;

        n = event_size;
    }

    return n;
}

/**
 * Flush the content of the binlog file to disk.
 *
 * @param   router  The binlog router
 */
void
blr_file_flush(ROUTER_INSTANCE *router)
{
    fsync(router->binlog_fd);
}

/**
 * Open a binlog file for reading binlog records
 *
 * @param router    The router instance
 * @param binlog    The binlog filename
 * @return a binlog file record
 */
BLFILE *
blr_open_binlog(ROUTER_INSTANCE *router, char *binlog)
{
    size_t len = strlen(binlog);
    if (len > BINLOG_FNAMELEN)
    {
        MXS_ERROR("The binlog filename %s is longer than the maximum allowed length %d.",
                  binlog, BINLOG_FNAMELEN);
        return NULL;
    }

    len += (strlen(router->binlogdir) + 1); // +1 for the /.
    if (len > PATH_MAX)
    {
        MXS_ERROR("The length of %s/%s is longer than the maximum allowed length %d.",
                  router->binlogdir, binlog, PATH_MAX);
        return NULL;
    }

    char path[PATH_MAX + 1] = "";
    BLFILE *file;

    spinlock_acquire(&router->fileslock);
    file = router->files;
    while (file && strcmp(file->binlogname, binlog) != 0)
    {
        file = file->next;
    }

    if (file)
    {
        file->refcnt++;
        spinlock_release(&router->fileslock);
        return file;
    }

    if ((file = (BLFILE *)MXS_CALLOC(1, sizeof(BLFILE))) == NULL)
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
        MXS_ERROR("Failed to open binlog file %s", path);
        MXS_FREE(file);
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
 * @param router    The router instance
 * @param file      File record
 * @param pos       Position of binlog record to read
 * @param hdr       Binlog header to populate
 * @param errmsg    Allocated BINLOG_ERROR_MSG_LEN bytes message error buffer
 * @param enc_ctx   Encryption context for binlog file being read
 * @return          The binlog record wrapped in a GWBUF structure
 */
GWBUF *
blr_read_binlog(ROUTER_INSTANCE *router,
                BLFILE *file,
                unsigned long pos,
                REP_HEADER *hdr,
                char *errmsg,
                const SLAVE_ENCRYPTION_CTX *enc_ctx)
{
    uint8_t hdbuf[BINLOG_EVENT_HDR_LEN];
    GWBUF *result;
    unsigned char *data;
    int n;
    unsigned long filelen = 0;
    struct stat statb;

    memset(hdbuf, '\0', BINLOG_EVENT_HDR_LEN);

    /* set error indicator */
    hdr->ok = SLAVE_POS_READ_ERR;

    if (!file)
    {
        snprintf(errmsg, BINLOG_ERROR_MSG_LEN,
                 "Invalid file pointer for requested binlog at position %lu", pos);
        return NULL;
    }

    spinlock_acquire(&file->lock);
    if (fstat(file->fd, &statb) == 0)
    {
        filelen = statb.st_size;
    }
    else
    {
        if (file->fd == -1)
        {
            hdr->ok = SLAVE_POS_BAD_FD;
            snprintf(errmsg, BINLOG_ERROR_MSG_LEN,
                     "blr_read_binlog called with invalid file->fd, pos %lu", pos);
            spinlock_release(&file->lock);
            return NULL;
        }
    }
    spinlock_release(&file->lock);

    if (pos > filelen)
    {
        spinlock_acquire(&router->binlog_lock);
        spinlock_acquire(&file->lock);

        if (strcmp(router->binlog_name, file->binlogname) != 0)
        {
            snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Requested position %lu is beyond "
                     "'closed' binlog file '%s', size %lu. Generating Error '1236'",
                     pos, file->binlogname, filelen);
        }
        else
        {
            snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Requested position %lu is beyond "
                     "end of the latest binlog file '%s', size %lu. Disconnecting",
                     pos, file->binlogname, filelen);

            /* Slave will be disconnected by the calling routine */
            hdr->ok = SLAVE_POS_BEYOND_EOF;

        }

        spinlock_release(&file->lock);
        spinlock_release(&router->binlog_lock);

        return NULL;
    }

    spinlock_acquire(&router->binlog_lock);
    spinlock_acquire(&file->lock);

    if (strcmp(router->binlog_name, file->binlogname) == 0 &&
        pos >= router->binlog_position)
    {
        if (pos > router->binlog_position)
        {
            snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Requested binlog position %lu is unsafe. "
                     "Latest safe position %lu, end of binlog file %lu",
                     pos, router->binlog_position, router->current_pos);

            hdr->ok = SLAVE_POS_READ_UNSAFE;
        }
        else
        {
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
            MXS_INFO("Reached end of binlog file '%s' at %lu.",
                     file->binlogname, pos);

            /* set ok indicator */
            hdr->ok = SLAVE_POS_READ_OK;

            break;
        case -1:
            {
                char err_msg[MXS_STRERROR_BUFLEN];
                snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Failed to read binlog file '%s'; (%s), event at %lu",
                         file->binlogname, strerror_r(errno, err_msg, sizeof(err_msg)), pos);

                if (errno == EBADF)
                {
                    snprintf(errmsg, BINLOG_ERROR_MSG_LEN,
                             "Bad file descriptor for binlog file '%s', "
                             "refcount %d, descriptor %d, event at %lu",
                             file->binlogname, file->refcnt, file->fd, pos);
                }
            }
            break;
        default:
            snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Bogus data in log event header; "
                     "expected %d bytes but read %d, position %lu, binlog file '%s'",
                     BINLOG_EVENT_HDR_LEN, n, pos, file->binlogname);
            break;
        }
        return NULL;
    }


    /* If enc_ctx is NULL check position */
    if (enc_ctx == NULL)
    {
        hdr->timestamp = EXTRACT32(hdbuf);
        hdr->event_type = hdbuf[4];
        hdr->serverid = EXTRACT32(&hdbuf[5]);
        hdr->event_size = extract_field(&hdbuf[9], 32);
        hdr->next_pos = EXTRACT32(&hdbuf[13]);
        hdr->flags = EXTRACT16(&hdbuf[17]);

        /**
         * Binlog event check based on Replication Header content and pos
         */
        if (!blr_binlog_event_check(router, pos, hdr, file->binlogname, errmsg))
        {
            return NULL;
        }

        /* Try to read again the binlog event */
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
                    MXS_INFO("Reached end of binlog file at %lu.",
                             pos);

                    /* set ok indicator */
                    hdr->ok = SLAVE_POS_READ_OK;
                    break;
                case -1:
                    {
                        char err_msg[MXS_STRERROR_BUFLEN];
                        snprintf(errmsg, BINLOG_ERROR_MSG_LEN,
                                 "Failed to reread header in binlog file '%s'; (%s), event at %lu",
                                 file->binlogname, strerror_r(errno, err_msg, sizeof(err_msg)), pos);

                        if (errno == EBADF)
                        {
                            snprintf(errmsg, BINLOG_ERROR_MSG_LEN,
                                     "Bad file descriptor rereading header for binlog file '%s', "
                                     "refcount %d, descriptor %d, event at %lu",
                                     file->binlogname, file->refcnt, file->fd, pos);
                        }
                    }
                    break;
                default:
                    snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Bogus data rereading log event header; "
                             "expected %d bytes but read %d, position %lu in binlog file '%s'",
                             BINLOG_EVENT_HDR_LEN, n, pos, file->binlogname);
                    break;
                }
                return NULL;
            }

            /* Fill replication header struct */
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
    }
    else
    {
        /**
         * The encryption context is set at this point.
         *
         * Only the event size is in "clear", use it.
        */
        hdr->event_size = extract_field(&hdbuf[9], 32);
    }

    /* Allocate memory for the binlog event */
    if ((result = gwbuf_alloc(hdr->event_size)) == NULL)
    {
        snprintf(errmsg, BINLOG_ERROR_MSG_LEN,
                 "Failed to allocate memory for binlog entry, size %d, event at %lu in binlog file '%s'",
                 hdr->event_size, pos, file->binlogname);
        return NULL;
    }

    data = GWBUF_DATA(result);

    memcpy(data, hdbuf, BINLOG_EVENT_HDR_LEN);  // Copy the header in the buffer

    if ((n = pread(file->fd, &data[BINLOG_EVENT_HDR_LEN], hdr->event_size - BINLOG_EVENT_HDR_LEN,
                   pos + BINLOG_EVENT_HDR_LEN))
        != hdr->event_size - BINLOG_EVENT_HDR_LEN)  // Read the balance
    {
        if (n ==  0)
        {
            MXS_INFO("Reached end of binlog file at %lu while reading remaining bytes.",
                     pos);

            /* set ok indicator */
            hdr->ok = SLAVE_POS_READ_OK;

            return NULL;
        }

        if (n == -1)
        {
            char err_msg[MXS_STRERROR_BUFLEN];
            snprintf(errmsg, BINLOG_ERROR_MSG_LEN,
                     "Error reading the binlog event at %lu in binlog file '%s';"
                     "(%s), expected %d bytes.",
                     pos,
                     file->binlogname,
                     strerror_r(errno, err_msg, sizeof(err_msg)),
                     hdr->event_size - BINLOG_EVENT_HDR_LEN);
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

    /**
     * Check whether we need to decrypt the current event.
     * Note: if event is before the first_enc_event_pos don't decrypt it
     */
    if (enc_ctx && pos >= enc_ctx->first_enc_event_pos)
    {
        GWBUF *decrypted_event;
        uint8_t *decrypt_ptr;
        /* prepare and decrypt the event */
        if ((decrypted_event = blr_prepare_encrypted_event(router,
                                                           data,
                                                           hdr->event_size,
                                                           pos,
                                                           enc_ctx->nonce,
                                                           BINLOG_FLAG_DECRYPT)) == NULL)
        {
            snprintf(errmsg, BINLOG_ERROR_MSG_LEN, "Binlog event decryption error: "
                     "file size is %lu, event at %lu in binlog file '%s'",
                     filelen, pos, file->binlogname);
            gwbuf_free(result);
            return NULL;
        }

        decrypt_ptr = GWBUF_DATA(decrypted_event);

        /* Fill replication header struct */
        hdr->timestamp = EXTRACT32(decrypt_ptr);
        hdr->event_type = decrypt_ptr[4];
        hdr->serverid = EXTRACT32(&decrypt_ptr[5]);
        hdr->event_size = extract_field(&decrypt_ptr[9], 32);
        hdr->next_pos = EXTRACT32(&decrypt_ptr[13]);
        hdr->flags = EXTRACT16(&decrypt_ptr[17]);

        /* Free data read from disk */
        gwbuf_free(result);

        /**
         * Binlog event check based on Replication Header content and pos
         */
        if (!blr_binlog_event_check(router, pos, hdr, file->binlogname, errmsg))
        {
            gwbuf_free(decrypted_event);
            return NULL;
        }

        /* Set the decrypted event as result*/
        result = decrypted_event;
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
 * @param router    The router instance
 * @param file      The file to close
 */
void
blr_close_binlog(ROUTER_INSTANCE *router, BLFILE *file)
{
    spinlock_acquire(&router->fileslock);
    file->refcnt--;
    if (file->refcnt == 0)
    {
        if (router->files == file)
        {
            router->files = file->next;
        }
        else
        {
            BLFILE  *ptr = router->files;
            while (ptr && ptr->next != file)
            {
                ptr = ptr->next;
            }
            if (ptr)
            {
                ptr->next = file->next;
            }
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
        MXS_FREE(file);
    }
}

/**
 * Log the event header of  binlog event
 *
 * @param   priority The syslog priority of the message (LOG_ERR, LOG_WARNING, etc.)
 * @param   msg  A message strign to preceed the header with
 * @param   ptr  The event header raw data
 */
static void
blr_log_header(int priority, char *msg, uint8_t *ptr)
{
    char buf[400], *bufp;
    int i;

    bufp = buf;
    bufp += sprintf(bufp, "%s: ", msg);
    for (i = 0; i < BINLOG_EVENT_HDR_LEN; i++)
    {
        bufp += sprintf(bufp, "0x%02x ", ptr[i]);
    }
    MXS_LOG_MESSAGE(priority, "%s", buf);
}

/**
 * Return the size of the current binlog file
 *
 * @param file  The binlog file
 * @return  The current size of the binlog file
 */
unsigned long
blr_file_size(BLFILE *file)
{
    struct stat statb;

    if (fstat(file->fd, &statb) == 0)
    {
        return statb.st_size;
    }
    return 0;
}


/**
 * Write the response packet to a cache file so that MaxScale can respond
 * even if there is no master running when MaxScale starts.
 *
 * cache dir is 'cache' under router->binlogdir
 *
 * @param router    The instance of the router
 * @param response  The name of the response, used to name the cached file
 * @param buf       The buffer to written to the cache
 */
void
blr_cache_response(ROUTER_INSTANCE *router, char *response, GWBUF *buf)
{
    static const char CACHE[] = "/cache";
    size_t len = strlen(router->binlogdir) + (sizeof(CACHE) - 1) + sizeof('/') + strlen(response);
    if (len > PATH_MAX)
    {
        MXS_ERROR("The cache path %s%s/%s is longer than the maximum allowed length %d.",
                  router->binlogdir, CACHE, response, PATH_MAX);
        return;
    }

    char path[PATH_MAX + 1] = "";
    int fd;

    strcpy(path, router->binlogdir);
    strcat(path, CACHE);

    if (access(path, R_OK) == -1)
    {
        // TODO: Check error, ENOENT or something else.
        mkdir(path, 0700);
        // TODO: Check return value.
    }

    strcat(path, "/");
    strcat(path, response);

    if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1)
    {
        return;
    }
    if (write(fd, GWBUF_DATA(buf), GWBUF_LENGTH(buf)) == -1)
    {
        char err[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed to write cached response: %d, %s",
                  errno, strerror_r(errno, err, sizeof(err)));
    }

    close(fd);
}

/**
 * Read a cached copy of a master response message. This allows
 * the router to start and serve any binlogs it already has on disk
 * if the master is not available.
 *
 * cache dir is 'cache' under router->binlogdir
 *
 * @param router    The router instance structure
 * @param response  The name of the response
 * @return A pointer to a GWBUF structure
 */
GWBUF *
blr_cache_read_response(ROUTER_INSTANCE *router, char *response)
{
    static const char CACHE[] = "/cache";
    size_t len = strlen(router->binlogdir) + (sizeof(CACHE) - 1) + sizeof('/') + strlen(response);
    if (len > PATH_MAX)
    {
        MXS_ERROR("The cache path %s%s/%s is longer than the maximum allowed length %d.",
                  router->binlogdir, CACHE, response, PATH_MAX);
        return NULL;
    }

    struct stat statb;
    char path[PATH_MAX + 1] = "";
    int fd;
    GWBUF *buf;

    strcpy(path, router->binlogdir);
    strcat(path, CACHE);
    strcat(path, "/");
    strcat(path, response);

    if ((fd = open(path, O_RDONLY)) == -1)
    {
        return NULL;
    }

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
    if (read(fd, GWBUF_DATA(buf), statb.st_size) == -1)
    {
        char err[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed to read cached response: %d, %s",
                  errno, strerror_r(errno, err, sizeof(err)));
    }
    close(fd);
    return buf;
}

/**
 * Does the next binlog file in the sequence for the slave exist.
 *
 * @param router    The router instance
 * @param slave     The slave in question
 * @return      0 if the next file does not exist
 */
int
blr_file_next_exists(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave)
{
    char *sptr, buf[BLRM_BINLOG_NAME_STR_LEN], bigbuf[PATH_MAX + 1];
    int filenum;

    if ((sptr = strrchr(slave->binlogfile, '.')) == NULL)
    {
        return 0;
    }
    filenum = atoi(sptr + 1);
    sprintf(buf, BINLOG_NAMEFMT, router->fileroot, filenum + 1);
    sprintf(bigbuf, "%s/%s", router->binlogdir, buf);
    if (access(bigbuf, R_OK) == -1)
    {
        return 0;
    }
    return 1;
}

/**
 * Read all replication events from a binlog file.
 *
 * Routine detects errors and pending transactions
 *
 * @param router  The router instance
 * @param fix     Whether to fix or not errors
 * @param debug   Whether to enable or not the debug for events
 * @return        0 on success, >0 on failure
 */
int
blr_read_events_all_events(ROUTER_INSTANCE *router, int fix, int debug)
{
    unsigned long filelen = 0;
    struct stat statb;
    uint8_t hdbuf[BINLOG_EVENT_HDR_LEN];
    uint8_t *data;
    GWBUF *result = NULL;
    GWBUF *decrypted_event = NULL;
    unsigned long long pos = 4;
    unsigned long long last_known_commit = 4;

    REP_HEADER hdr;
    int pending_transaction = 0;
    int n;
    int db_name_len;
    uint8_t *ptr;
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
    int start_encryption_seen = 0;

    memset(&first_event, '\0', sizeof(first_event));
    memset(&last_event, '\0', sizeof(last_event));
    memset(&fde_event, '\0', sizeof(fde_event));

    if (router->binlog_fd == -1)
    {
        MXS_ERROR("Current binlog file %s is not open",
                  router->binlog_name);
        return 1;
    }

    if (fstat(router->binlog_fd, &statb) == 0)
    {
        filelen = statb.st_size;
    }

    router->current_pos = 4;
    router->binlog_position = 4;
    router->current_safe_event = 4;

    while (1)
    {

        /* Read the header information from the file */
        if ((n = pread(router->binlog_fd, hdbuf, BINLOG_EVENT_HDR_LEN, pos)) != BINLOG_EVENT_HDR_LEN)
        {
            switch (n)
            {
            case 0:
                MXS_DEBUG("End of binlog file [%s] at %llu.",
                          router->binlog_name,
                          pos);
                if (n_transactions)
                {
                    average_events = (double)((double)total_events / (double)n_transactions) * (1.0);
                }
                if (n_transactions)
                {
                    average_bytes = (double)((double)total_bytes / (double)n_transactions) * (1.0);
                }

                /* Report Binlog First and Last event */
                if (pos > 4)
                {
                    if (first_event.event_type == 0)
                    {
                        blr_print_binlog_details(router, fde_event, last_event);
                    }
                    else
                    {
                        blr_print_binlog_details(router, first_event, last_event);
                    }
                }

                /* Report Transaction Summary */
                if (n_transactions != 0)
                {
                    char total_label[2] = "";
                    char average_label[2] = "";
                    char max_label[2] = "";
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

                if (pending_transaction)
                {
                    MXS_WARNING("Binlog file %s contains a previous Opened "
                                "Transaction @ %llu. This pos is safe for slaves",
                                router->binlog_name,
                                last_known_commit);

                }
                break;
            case -1:
                {
                    char err[MXS_STRERROR_BUFLEN];
                    MXS_ERROR("Failed to read binlog file %s at position %llu"
                              " (%s).", router->binlog_name, pos,
                              strerror_r(errno, err, sizeof(err)));

                    if (errno == EBADF)
                    {
                        MXS_ERROR("Bad file descriptor in read binlog for file %s"
                                  ", descriptor %d.",
                                  router->binlog_name, router->binlog_fd);
                    }
                }
                break;
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

            if (pending_transaction)
            {
                router->binlog_position = last_known_commit;
                router->current_safe_event = last_known_commit;
                router->current_pos = pos;
                router->pending_transaction = 1;

                MXS_ERROR("Binlog '%s' ends at position %lu and has an incomplete transaction at %lu. ",
                          router->binlog_name, router->current_pos, router->binlog_position);

                return 0;
            }
            else
            {
                /* any error */
                if (n != 0)
                {
                    router->binlog_position = last_known_commit;
                    router->current_safe_event = last_known_commit;
                    router->current_pos = pos;

                    MXS_WARNING("an error has been found. "
                                "Setting safe pos to %lu, current pos %lu",
                                router->binlog_position, router->current_pos);
                    if (fix)
                    {
                        if (ftruncate(router->binlog_fd, router->binlog_position) == 0)
                        {
                            MXS_NOTICE("Binlog file %s has been truncated at %lu",
                                       router->binlog_name,
                                       router->binlog_position);
                            fsync(router->binlog_fd);
                        }
                    }

                    return 1;
                }
                else
                {
                    router->binlog_position = pos;
                    router->current_safe_event = pos;
                    router->current_pos = pos;

                    return 0;
                }
            }
        }

        if (start_encryption_seen)
        {
            uint8_t iv[AES_BLOCK_SIZE + 1] = "";
            char iv_hex[AES_BLOCK_SIZE * 2 + 1] = "";
            /* The event size, 4 bytes, is written in clear: use it */
            uint32_t event_size = EXTRACT32(hdbuf + BINLOG_EVENT_LEN_OFFSET);

            /**
             * Events are encrypted.
             *
             * Print the IV for the current encrypted event.
             */

            if (debug & BLR_REPORT_REP_HEADER)
            {
                /* Get binlog file "nonce" and other data from router encryption_ctx */
                BINLOG_ENCRYPTION_CTX *enc_ctx = router->encryption_ctx;

                /* Encryption IV is 12 bytes nonce + 4 bytes event position */
                memcpy(iv, enc_ctx->nonce, BLRM_NONCE_LENGTH);
                gw_mysql_set_byte4(iv + BLRM_NONCE_LENGTH, (unsigned long)pos);

                /* Human readable version */
                gw_bin2hex(iv_hex, iv, BLRM_IV_LENGTH);

                MXS_DEBUG("** Encrypted Event @ %lu: the IV is %s, size is %lu, next pos is %lu\n",
                          (unsigned long)pos,
                          iv_hex, (unsigned long)event_size,
                          (unsigned long)(pos + event_size));
            }

            /* Set event size only in hdr struct, before decryption */
            hdr.event_size = event_size;

        }
        else
        {
            char errmsg[BINLOG_ERROR_MSG_LEN + 1] = "";
            /* fill replication header struct */
            hdr.timestamp = EXTRACT32(hdbuf);
            hdr.event_type = hdbuf[4];
            hdr.serverid = EXTRACT32(&hdbuf[5]);
            hdr.event_size = extract_field(&hdbuf[9], 32);
            hdr.next_pos = EXTRACT32(&hdbuf[13]);
            hdr.flags = EXTRACT16(&hdbuf[17]);

            /* Check event */
            if (!blr_binlog_event_check(router,
                                        pos,
                                        &hdr,
                                        router->binlog_name,
                                        errmsg))
            {
                router->binlog_position = last_known_commit;
                router->current_safe_event = last_known_commit;
                router->current_pos = pos;

                MXS_WARNING("an error has been found in %s. "
                            "Setting safe pos to %lu, current pos %lu. "
                            "ErrMsg [%s]",
                            router->binlog_name,
                            router->binlog_position,
                            router->current_pos,
                            errmsg);

                if (fix)
                {
                    if (ftruncate(router->binlog_fd, router->binlog_position) == 0)
                    {
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
                if (fix)
                {
                    if (ftruncate(router->binlog_fd, router->binlog_position) == 0)
                    {
                        MXS_NOTICE("Binlog file %s has been truncated at %lu",
                                   router->binlog_name,
                                   router->binlog_position);
                        fsync(router->binlog_fd);
                    }
                }

                return 1;
            }
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

            if (fix)
            {
                if (ftruncate(router->binlog_fd, router->binlog_position) == 0)
                {
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
        if ((n = pread(router->binlog_fd, &data[BINLOG_EVENT_HDR_LEN],
                       hdr.event_size - BINLOG_EVENT_HDR_LEN,
                       pos + BINLOG_EVENT_HDR_LEN)) != hdr.event_size - BINLOG_EVENT_HDR_LEN)
        {
            if (n == -1)
            {
                char err[MXS_STRERROR_BUFLEN];
                MXS_ERROR("Error reading the event at %llu in %s. "
                          "%s, expected %d bytes.",
                          pos, router->binlog_name,
                          strerror_r(errno, err, sizeof(err)),
                          hdr.event_size - BINLOG_EVENT_HDR_LEN);
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
            if (fix)
            {
                if (ftruncate(router->binlog_fd, router->binlog_position) == 0)
                {
                    MXS_NOTICE("Binlog file %s has been truncated at %lu",
                               router->binlog_name,
                               router->binlog_position);
                    fsync(router->binlog_fd);
                }
            }

            return 1;
        }

        /* check for pending transaction */
        if (pending_transaction == 0)
        {
            last_known_commit = pos;
        }

        /* get firts event timestamp, after FDE */
        if (fde_seen)
        {
            first_event.event_time = (unsigned long)hdr.timestamp;
            first_event.event_type = hdr.event_type;
            first_event.event_pos = pos;
            fde_seen = 0;
        }

        /* decrypt events */
        if (start_encryption_seen)
        {
            uint8_t iv[AES_BLOCK_SIZE + 1] = "";
            char iv_hex[AES_BLOCK_SIZE * 2 + 1] = "";
            uint32_t event_size = EXTRACT32(hdbuf + BINLOG_EVENT_LEN_OFFSET);
            uint8_t *decrypt_ptr;
            unsigned long next_pos;
            char errmsg[BINLOG_ERROR_MSG_LEN + 1] = "";

            /**
             * Events are encrypted.
             */

            if ((decrypted_event = blr_prepare_encrypted_event(router,
                                                               data,
                                                               hdr.event_size,
                                                               pos,
                                                               NULL,
                                                               BINLOG_FLAG_DECRYPT)) == NULL)
            {
                MXS_ERROR("Error while decrypting event at pos %lu, size %lu",
                          (unsigned long)pos,
                          (unsigned long)hdr.event_size);
                router->m_errno = BINLOG_FATAL_ERROR_READING;
                gwbuf_free(result);
                return 1;
            }

            decrypt_ptr = GWBUF_DATA(decrypted_event);

            /* fill replication header struct */
            hdr.timestamp = EXTRACT32(decrypt_ptr);
            hdr.event_type = decrypt_ptr[4];
            hdr.serverid = EXTRACT32(&decrypt_ptr[5]);
            hdr.event_size = extract_field(&decrypt_ptr[9], 32);
            hdr.next_pos = EXTRACT32(&decrypt_ptr[13]);
            hdr.flags = EXTRACT16(&decrypt_ptr[17]);

            /* Check event */
            if (!blr_binlog_event_check(router,
                                        pos,
                                        &hdr,
                                        router->binlog_name,
                                        errmsg))
            {
                router->m_errno = BINLOG_FATAL_ERROR_READING;
                gwbuf_free(decrypted_event);
                gwbuf_free(result);
                MXS_ERROR("Error while decrypting event: %s", errmsg);
                return 1;
            }

            /* get event content after event header */
            ptr = decrypt_ptr + BINLOG_EVENT_HDR_LEN;
        }
        else
        {
            /* get event content after event header */
            ptr = data + BINLOG_EVENT_HDR_LEN;
        }

        /* check for FORMAT DESCRIPTION EVENT */
        if (hdr.event_type == FORMAT_DESCRIPTION_EVENT)
        {
            int event_header_length;
            int check_alg;
            uint8_t *checksum;
            char    buf_t[40];
            struct  tm  tm_t;

            fde_seen = 1;
            fde_event.event_time = (unsigned long)hdr.timestamp;
            fde_event.event_type = hdr.event_type;
            fde_event.event_pos = pos;

            localtime_r(&fde_event.event_time, &tm_t);
            asctime_r(&tm_t, buf_t);

            if (buf_t[strlen(buf_t) - 1] == '\n')
            {
                buf_t[strlen(buf_t) - 1] = '\0';
            }

            if (debug)
            {
                MXS_DEBUG("- Format Description event FDE @ %llu, size %lu, time %lu (%s)",
                          pos, (unsigned long)hdr.event_size, fde_event.event_time, buf_t);
            }

            /* FDE is:
             *
             * 2 bytes          binlog-version
             * string[50]       mysql-server version
             * 4 bytes          create timestamp
             * 1                event header length, 19 is the current length
             * string[p]        event type header lengths:
             *                  an array indexed by [Binlog Event Type - 1]
             */

            /* ptr now points to event_header_length byte.
             * This offset is just 1 byte before the number of supported events offset
             */
            event_header_length =  ptr[BLRM_FDE_EVENT_TYPES_OFFSET - 1];

            /* The number of supported events formula:
             * number_of_events = event_size - (event_header_len + BLRM_FDE_EVENT_TYPES_OFFSET)
             */
            int n_events = hdr.event_size - event_header_length - BLRM_FDE_EVENT_TYPES_OFFSET;

            /**
             * The FDE event also carries 5 additional bytes:
             *
             * 1 byte is the checksum_alg_type and 4 bytes are the computed crc32
             *
             * These 5 bytes are always present even if alg_type is NONE/UNDEF:
             * then the 4 crc32 bytes must not be checked, whatever the value is.
             *
             * In case of CRC32 algo_type the 4 bytes contain the event crc32.
             */
            int fde_extra_bytes = BINLOG_EVENT_CRC_ALGO_TYPE + BINLOG_EVENT_CRC_SIZE;

            /* Now remove from the calculated number of events the extra 5 bytes */
            n_events -= fde_extra_bytes;

            if (debug)
            {
                MXS_DEBUG("       FDE ServerVersion [%50s]", ptr + 2);

                MXS_DEBUG("       FDE Header EventLength %i"
                          ", N. of supported MySQL/MariaDB events %i",
                          event_header_length,
                          n_events);
            }

            /* Check whether Master is sending events with CRC32 checksum */
            checksum = ptr + hdr.event_size - event_header_length - fde_extra_bytes;
            check_alg = checksum[0];

            if (debug)
            {
                MXS_DEBUG("       FDE Checksum alg desc %i, alg type %s",
                          check_alg,
                          check_alg == 1 ?
                          "BINLOG_CHECKSUM_ALG_CRC32" : "NONE or UNDEF");
            }
            if (check_alg == 1)
            {
                /* Set checksum found indicator */
                found_chksum = 1;
            }
            else
            {
                found_chksum = 0;
            }
        }

        if ((debug & BLR_REPORT_REP_HEADER))
        {
            char *event_desc = blr_get_event_description(router, hdr.event_type);
            MXS_DEBUG("%8s==== Event Header ====\n%39sEvent time %lu\n%39sEvent Type %u (%s)\n%39sServer Id %lu\n%39sNextPos %lu\n%39sFlags %u",
                      " ", " ", (unsigned long)hdr.timestamp, " ", hdr.event_type,
                      event_desc ? event_desc : "NULL", " ",
                      (unsigned long)hdr.serverid, " ", (unsigned long)hdr.next_pos, " ", hdr.flags);
            if (found_chksum)
            {
                char hex_checksum[BINLOG_EVENT_CRC_SIZE * 2 + strlen(BLR_REPORT_CHECKSUM_FORMAT) + 1];
                blr_report_checksum(hdr, ptr, hex_checksum);
                MXS_DEBUG("%8s%s", " ", hex_checksum);
            }
        }

        /* Detect possible Start Encryption Event */
        if (hdr.event_type == MARIADB10_START_ENCRYPTION_EVENT)
        {
            char nonce_hex[AES_BLOCK_SIZE * 2 + 1] = "";
            START_ENCRYPTION_EVENT ste_event = {};
            BINLOG_ENCRYPTION_CTX *new_encryption_ctx = MXS_CALLOC(1, sizeof(BINLOG_ENCRYPTION_CTX));

            if (new_encryption_ctx == NULL)
            {
                router->m_errno = BINLOG_FATAL_ERROR_READING;
                return 1;
            }

            /* The start encryption event data is 17 bytes long:
             * Scheme = 1
             * Key Version: 4
             * nonce = 12
             */

            /* Fill the event content, after the event header */
            ste_event.binlog_crypto_scheme = ptr[0];
            ste_event.binlog_key_version = extract_field(ptr + 1, 32);
            memcpy(ste_event.nonce, ptr + 1 + 4, BLRM_NONCE_LENGTH);

            /* Fill the encryption_ctx */
            memcpy(new_encryption_ctx->nonce, ste_event.nonce, BLRM_NONCE_LENGTH);
            new_encryption_ctx->binlog_crypto_scheme = ste_event.binlog_crypto_scheme;
            memcpy(&new_encryption_ctx->binlog_key_version,
                   &ste_event.binlog_key_version, BLRM_KEY_VERSION_LENGTH);

            if (debug)
            {
                /* Hex representation of nonce */
                gw_bin2hex(nonce_hex, ste_event.nonce, BLRM_NONCE_LENGTH);

                MXS_DEBUG("- START_ENCRYPTION event @ %llu, size %lu, next pos is @ %lu, flags %u",
                          pos, (unsigned long)hdr.event_size, (unsigned long)hdr.next_pos, hdr.flags);

                MXS_DEBUG("        Encryption scheme: %u, key_version: %u,"
                          " nonce: %s\n", ste_event.binlog_crypto_scheme,
                          ste_event.binlog_key_version, nonce_hex);
            }

            if (router->encryption.key_len == 0)
            {
                router->m_errno = BINLOG_FATAL_ERROR_READING;
                MXS_ERROR("*** The binlog is encrypted. No KEY/Algo found for decryption. ***");
                return 1;
            }

            start_encryption_seen = 1;

            /* Update the router encryption context */
            MXS_FREE(router->encryption_ctx);
            router->encryption_ctx = new_encryption_ctx;
        }

        /* set last event time, pos and type */
        last_event.event_time = (unsigned long)hdr.timestamp;
        last_event.event_type = hdr.event_type;
        last_event.event_pos = pos;

        /* Decode ROTATE EVENT */
        if (hdr.event_type == ROTATE_EVENT)
        {
            int len, slen;
            uint64_t new_pos;
            char file[BINLOG_FNAMELEN + 1];

            len = hdr.event_size - BINLOG_EVENT_HDR_LEN;
            new_pos = extract_field(ptr + 4, 32);
            new_pos <<= 32;
            new_pos |= extract_field(ptr, 32);
            slen = len - (8 + 4);           // Allow for position and CRC
            if (found_chksum == 0)
            {
                slen += 4;
            }
            if (slen > BINLOG_FNAMELEN)
            {
                slen = BINLOG_FNAMELEN;
            }
            memcpy(file, ptr + 8, slen);
            file[slen] = 0;

            if (debug)
            {
                MXS_DEBUG("- Rotate event @ %llu, next file is [%s] @ %lu",
                          pos, file, new_pos);
            }
        }

        /* If MariaDB 10 compatibility:
         * check for MARIADB10_GTID_EVENT with flags = 0
         * This marks the transaction starts instead of
         * QUERY_EVENT with "BEGIN"
         */

        if (router->mariadb10_compat)
        {
            if (hdr.event_type == MARIADB10_GTID_EVENT)
            {
                uint64_t n_sequence;/* 8 bytes */
                uint32_t domainid;  /* 4 bytes */
                unsigned int flags; /* 1 byte */
                n_sequence = extract_field(ptr, 64);
                domainid = extract_field(ptr + 8, 32);
                flags = *(ptr + 8 + 4);

                if ((flags & (MARIADB_FL_DDL | MARIADB_FL_STANDALONE)) == 0)
                {
                    if (pending_transaction > 0)
                    {
                        MXS_ERROR("Transaction cannot be @ pos %llu: "
                                  "Another MariaDB 10 transaction (GTID %u-%u-%lu)"
                                  " was opened at %llu",
                                  pos, domainid, hdr.serverid,
                                  n_sequence, last_known_commit);

                        gwbuf_free(result);

                        break;
                    }
                    else
                    {
                        pending_transaction = 1;

                        transaction_events = 0;
                        event_bytes = 0;

                        if (debug)
                        {
                            MXS_DEBUG("> MariaDB 10 Transaction (GTID %u-%u-%lu)"
                                      " starts @ pos %llu",
                                      domainid, hdr.serverid, n_sequence, pos);
                        }
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

        if (hdr.event_type == QUERY_EVENT)
        {
            char *statement_sql;
            db_name_len = ptr[4 + 4];
            var_block_len = ptr[4 + 4 + 1 + 2];

            statement_len =
                hdr.event_size -
                BINLOG_EVENT_HDR_LEN -
                (4 + 4 + 1 + 2 + 2 + var_block_len + 1 + db_name_len);

            statement_sql = MXS_CALLOC(1, statement_len + 1);
            if (statement_sql)
            {
                memcpy(statement_sql,
                       (char *)ptr + 4 + 4 + 1 + 2 + 2 + var_block_len + 1 + db_name_len,
                       statement_len);

                /* A transaction starts with this event */
                if (strncmp(statement_sql, "BEGIN", 5) == 0)
                {
                    if (pending_transaction > 0)
                    {
                        MXS_ERROR("Transaction cannot be @ pos %llu: "
                                  "Another transaction was opened at %llu",
                                  pos, last_known_commit);

                        MXS_FREE(statement_sql);
                        gwbuf_free(result);

                        break;
                    }
                    else
                    {
                        pending_transaction = 1;

                        transaction_events = 0;
                        event_bytes = 0;

                        if (debug)
                        {
                            MXS_DEBUG("> Transaction starts @ pos %llu", pos);
                        }
                    }
                }

                /* Commit received for non transactional tables, i.e. MyISAM */
                if (strncmp(statement_sql, "COMMIT", 6) == 0)
                {
                    if (pending_transaction > 0)
                    {
                        pending_transaction = 3;

                        if (debug)
                        {
                            MXS_DEBUG("       Transaction @ pos %llu, closing @ %llu",
                                      last_known_commit, pos);
                        }
                    }
                }
                MXS_FREE(statement_sql);
            }
            else
            {
                MXS_ERROR("Unable to allocate memory for statement SQL in blr_file.c ");
                gwbuf_free(result);
                break;
            }

        }

        if (hdr.event_type == XID_EVENT)
        {
            /* Commit received for a transactional tables, i.e. InnoDB */

            if (pending_transaction > 0)
            {
                pending_transaction = 2;
                if (debug)
                {
                    MXS_DEBUG("       Transaction XID @ pos %llu, closing @ %llu",
                              last_known_commit, pos);
                }
            }
        }

        if (pending_transaction > 1)
        {
            if (debug)
            {
                MXS_DEBUG("< Transaction @ pos %llu, is now closed @ %llu. %lu events seen",
                          last_known_commit, pos, transaction_events);
            }
            pending_transaction = 0;
            last_known_commit = pos;

            total_events += transaction_events;

            if (transaction_events > max_events)
            {
                max_events = transaction_events;
            }

            n_transactions++;
        }

        gwbuf_free(result);
        gwbuf_free(decrypted_event);

        /* pos and next_pos sanity checks */
        if (hdr.next_pos > 0 && hdr.next_pos < pos)
        {
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
            if (fix)
            {
                if (ftruncate(router->binlog_fd, router->binlog_position) == 0)
                {
                    MXS_NOTICE("Binlog file %s has been truncated at %lu",
                               router->binlog_name,
                               router->binlog_position);
                    fsync(router->binlog_fd);
                }
            }

            return 2;
        }

        if (hdr.next_pos > 0 && hdr.next_pos != (pos + hdr.event_size))
        {
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

            if (fix)
            {
                if (ftruncate(router->binlog_fd, router->binlog_position) == 0)
                {
                    MXS_NOTICE("Binlog file %s has been truncated at %lu",
                               router->binlog_name,
                               router->binlog_position);
                    fsync(router->binlog_fd);
                }
            }

            return 2;
        }

        /* set pos to new value */
        if (hdr.next_pos > 0)
        {
            if (pending_transaction)
            {
                total_bytes += hdr.event_size;
                event_bytes += hdr.event_size;

                if (event_bytes > max_bytes)
                {
                    max_bytes = event_bytes;
                }
            }

            pos = hdr.next_pos;
        }
        else
        {
            MXS_ERROR("Current event type %d @ %llu has nex pos = %u : exiting",
                      hdr.event_type, pos, hdr.next_pos);
            break;
        }

        transaction_events++;
    }

    if (pending_transaction)
    {
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
    }
    else
    {
        router->binlog_position = pos;
        router->current_safe_event = pos;
        router->current_pos = pos;

        return 0;
    }
}

/**
 * Format a number to G, M, k, or B size
 *
 * @param event_size    The number to format
 * @param label     Label to use for display the formattted number
 */
static void
blr_format_event_size(double *event_size, char *label)
{
    if (*event_size > (1024 * 1024 * 1024))
    {
        *event_size = *event_size / (1024 * 1024 * 1024);
        label[0] = 'G';
    }
    else if (*event_size > (1024 * 1024))
    {
        *event_size = *event_size / (1024 * 1024);
        label[0] = 'M';
    }
    else if (*event_size > 1024)
    {
        *event_size = *event_size / (1024);
        label[0] = 'k';
    }
    else
    {
        label[0] = 'B';
    }
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
 * @param router    The router instance
 * @return      0 on error, >0 as sequence number
 */
int
blr_file_get_next_binlogname(ROUTER_INSTANCE *router)
{
    char *sptr;
    int filenum;

    if ((sptr = strrchr(router->binlog_name, '.')) == NULL)
    {
        return 0;
    }
    filenum = atoi(sptr + 1);
    if (filenum)
    {
        filenum++;
    }

    return filenum;
}

/**
 * Create a new binlog file
 *
 * @param router    The router instance
 * @param file      The new binlog file
 * @return      1 on success, 0 on failure
 */
int
blr_file_new_binlog(ROUTER_INSTANCE *router, char *file)
{
    return blr_file_create(router, file);
}

/**
 * Write a new ini file with master configuration
 *
 * File is 'inst->binlogdir/master.ini.tmp'
 * When done it's renamed to 'inst->binlogdir/master.ini'
 *
 * @param router    The current router instance
 * @param error     Preallocated error message
 * @return      0 on success, >0 on failure
 *
 */
int
blr_file_write_master_config(ROUTER_INSTANCE *router, char *error)
{
    char *section = "binlog_configuration";
    FILE *config_file;
    int rc;
    static const char MASTER_INI[] = "master.ini";
    static const char TMP[] = "tmp";
    size_t len = strlen(router->binlogdir);

    char filename[len + sizeof('/') + sizeof(MASTER_INI)]; // sizeof includes NULL
    char tmp_file[len + sizeof('/') + sizeof(MASTER_INI) + sizeof('.') + sizeof(TMP)];
    char err_msg[MXS_STRERROR_BUFLEN];
    char *ssl_ca;
    char *ssl_cert;
    char *ssl_key;
    char *ssl_version;

    sprintf(filename, "%s/%s", router->binlogdir, MASTER_INI);
    sprintf(tmp_file, "%s/%s.%s", router->binlogdir, MASTER_INI, TMP);

    /* open file for writing */
    config_file = fopen(tmp_file, "wb");
    if (config_file == NULL)
    {
        snprintf(error, BINLOG_ERROR_MSG_LEN, "%s, errno %u",
                 strerror_r(errno, err_msg, sizeof(err_msg)), errno);
        return 2;
    }

    if (chmod(tmp_file, S_IRUSR | S_IWUSR) < 0)
    {
        fclose(config_file);
        snprintf(error, BINLOG_ERROR_MSG_LEN, "%s, errno %u",
                 strerror_r(errno, err_msg, sizeof(err_msg)), errno);
        return 2;
    }

    /* write ini file section */
    fprintf(config_file, "[%s]\n", section);

    /* write ini file key=value */
    fprintf(config_file, "master_host=%s\n", router->service->dbref->server->name);
    fprintf(config_file, "master_port=%d\n", router->service->dbref->server->port);
    fprintf(config_file, "master_user=%s\n", router->user);
    fprintf(config_file, "master_password=%s\n", router->password);
    fprintf(config_file, "filestem=%s\n", router->fileroot);

    /* Add SSL options */
    if (router->ssl_enabled)
    {
        /* Use current settings */
        ssl_ca = router->service->dbref->server->server_ssl->ssl_ca_cert;
        ssl_cert = router->service->dbref->server->server_ssl->ssl_cert;
        ssl_key = router->service->dbref->server->server_ssl->ssl_key;
    }
    else
    {
        /* Try using previous configuration settings */
        ssl_ca = router->ssl_ca;
        ssl_cert = router->ssl_cert;
        ssl_key = router->ssl_key;
    }

    ssl_version = router->ssl_version;

    if (ssl_key && ssl_cert && ssl_ca)
    {
        fprintf(config_file, "master_ssl=%d\n", router->ssl_enabled);
        fprintf(config_file, "master_ssl_key=%s\n", ssl_key);
        fprintf(config_file, "master_ssl_cert=%s\n", ssl_cert);
        fprintf(config_file, "master_ssl_ca=%s\n", ssl_ca);
    }
    if (ssl_version && strlen(ssl_version))
    {
        fprintf(config_file, "master_tls_version=%s\n", ssl_version);
    }

    fclose(config_file);

    /* rename tmp file to right filename */
    rc = rename(tmp_file, filename);

    if (rc == -1)
    {
        snprintf(error, BINLOG_ERROR_MSG_LEN, "%s, errno %u",
                 strerror_r(errno, err_msg, sizeof(err_msg)), errno);
        return 3;
    }

    if (chmod(filename, S_IRUSR | S_IWUSR) < 0)
    {
        snprintf(error, BINLOG_ERROR_MSG_LEN, "%s, errno %u",
                 strerror_r(errno, err_msg, sizeof(err_msg)), errno);
        return 3;
    }

    return 0;
}

/** Print Binlog Details
 *
 * @param router        The router instance
 * @param first_event   First Event details
 * @param last_event    First Event details
 */

static void
blr_print_binlog_details(ROUTER_INSTANCE *router,
                         BINLOG_EVENT_DESC first_event,
                         BINLOG_EVENT_DESC last_event)
{
    char buf_t[40];
    struct tm tm_t;
    char *event_desc;

    /* First Event */
    localtime_r(&first_event.event_time, &tm_t);
    asctime_r(&tm_t, buf_t);

    if (buf_t[strlen(buf_t) - 1] == '\n')
    {
        buf_t[strlen(buf_t) - 1] = '\0';
    }

    event_desc = blr_get_event_description(router, first_event.event_type);

    MXS_NOTICE("%lu @ %" PRIu64 ", %s, (%s), First EventTime",
               first_event.event_time, first_event.event_pos,
               event_desc != NULL ? event_desc : "unknown", buf_t);

    /* Last Event */
    localtime_r(&last_event.event_time, &tm_t);
    asctime_r(&tm_t, buf_t);

    if (buf_t[strlen(buf_t) - 1] == '\n')
    {
        buf_t[strlen(buf_t) - 1] = '\0';
    }

    event_desc = blr_get_event_description(router, last_event.event_type);

    MXS_NOTICE("%lu @ %" PRIu64 ", %s, (%s), Last EventTime",
               last_event.event_time, last_event.event_pos,
               event_desc != NULL ? event_desc : "unknown", buf_t);
}

/** Create an ignorable event
 *
 * @param event_size     The size of the new event being created (crc32 4 bytes could be included)
 * @param hdr            Current replication event header, received from master
 * @param event_pos      The position in binlog file of the new event
 * @param do_checksum    Whether checksum must be calculated and stored
 * @return               Returns the pointer of new event
 */
static uint8_t *
blr_create_ignorable_event(uint32_t event_size,
                           REP_HEADER *hdr,
                           uint32_t event_pos,
                           bool do_checksum)
{
    uint8_t *new_event;

    if (event_size < BINLOG_EVENT_HDR_LEN)
    {
        MXS_ERROR("blr_create_ignorable_event an event of %lu bytes"
                  " is not valid in blr_file.c", (unsigned long)event_size);
        return NULL;
    }

    // Allocate space for event: size might contain the 4 crc32
    new_event = MXS_CALLOC(1, event_size);
    if (new_event == NULL)
    {
        return NULL;
    }

    // Populate Event header 19 bytes for Ignorable Event
    encode_value(&new_event[0], hdr->timestamp, 32); // same timestamp as in current received event
    new_event[4] = IGNORABLE_EVENT; // type is IGNORABLE_EVENT
    encode_value(&new_event[5], hdr->serverid, 32); // same serverid as in current received event
    encode_value(&new_event[9], event_size, 32); // event size
    encode_value(&new_event[13], event_pos + event_size, 32); // next_pos
    encode_value(&new_event[17], LOG_EVENT_IGNORABLE_F, 16); // flag is LOG_EVENT_IGNORABLE_F

    /* if checksum is required calculate the crc32 and add it in the last 4 bytes*/
    if (do_checksum)
    {
        /*
         * Now add the CRC to the Ignorable binlog event.
         *
         * The algorithm is first to compute the checksum of an empty buffer
         * and then the checksum of the real event: 4 byte less than event_size
         */
        uint32_t chksum;
        chksum = crc32(0L, NULL, 0);
        chksum = crc32(chksum, new_event, event_size - BINLOG_EVENT_CRC_SIZE);

        // checksum is stored after current event data using 4 bytes
        encode_value(new_event + event_size - BINLOG_EVENT_CRC_SIZE, chksum, 32);
    }
    return new_event;
}

/**
 * Create and write a special event (not received from master) into binlog file
 *
 * @param router        The current router instance
 * @param file_offset   Position where event will be written
 * @param event_size    The size of new event (it might hold the 4 bytes crc32)
 * @param hdr           Replication header of the current reived event (from Master)
 * @param type          Type of special event to create and write
 * @return              1 on success, 0 on error
 */
static int
blr_write_special_event(ROUTER_INSTANCE *router, uint32_t file_offset, uint32_t event_size, REP_HEADER *hdr,
                        int type)
{
    int n;
    uint8_t *new_event;
    char *new_event_desc;

    switch (type)
    {
    case BLRM_IGNORABLE:
        new_event_desc = "IGNORABLE";
        MXS_INFO("Hole detected while writing in binlog '%s' @ %lu: an %s event "
                 "of %lu bytes will be written at pos %lu",
                 router->binlog_name,
                 router->current_pos,
                 new_event_desc,
                 (unsigned long)event_size,
                 (unsigned long)file_offset);

        /* Create the Ignorable event */
        if ((new_event = blr_create_ignorable_event(event_size,
                                                    hdr,
                                                    file_offset,
                                                    router->master_chksum)) == NULL)
        {
            return 0;
        }
        if (router->encryption.enabled && router->encryption_ctx != NULL)
        {
            GWBUF *encrypted;
            uint8_t *encr_ptr;
            if ((encrypted = blr_prepare_encrypted_event(router,
                                                         new_event,
                                                         event_size,
                                                         router->current_pos,
                                                         NULL,
                                                         BINLOG_FLAG_ENCRYPT)) == NULL)
            {
                return 0;
            }

            memcpy(new_event, GWBUF_DATA(encrypted), event_size);
            gwbuf_free(encrypted);
        }
        break;
    case BLRM_START_ENCRYPTION:
        new_event_desc = "MARIADB10_START_ENCRYPTION";
        MXS_INFO("New event %s is being added in binlog '%s' @ %lu: "
                 "%lu bytes will be written at pos %lu",
                 new_event_desc,
                 router->binlog_name,
                 router->current_pos,
                 (unsigned long)event_size,
                 (unsigned long)file_offset);

        /* Create the MARIADB10_START_ENCRYPTION event */
        if ((new_event = blr_create_start_encryption_event(router,
                                                           file_offset,
                                                           router->master_chksum)) == NULL)
        {
            return 0;
        }
        break;
    default:
        new_event_desc = "UNKNOWN";
        MXS_ERROR("Cannot create special binlog event of %s type and size %lu "
                  "in binlog file '%s' @ %lu",
                  new_event_desc,
                  (unsigned long)event_size,
                  router->binlog_name,
                  router->current_pos);
        return 0;
        break;
    }

    /* Write the event */
    if ((n = pwrite(router->binlog_fd, new_event, event_size, router->last_written)) != event_size)
    {
        char err_msg[MXS_STRERROR_BUFLEN];
        MXS_ERROR("%s: Failed to write %s special binlog record at %lu of %s, %s. "
                  "Truncating to previous record.",
                  router->service->name, new_event_desc, (unsigned long)file_offset,
                  router->binlog_name,
                  strerror_r(errno, err_msg, sizeof(err_msg)));

        /* Remove any partial event that was written */
        if (ftruncate(router->binlog_fd, router->binlog_position))
        {
            MXS_ERROR("%s: Failed to truncate %s special binlog record at %lu of %s, %s. ",
                      router->service->name, new_event_desc, (unsigned long)file_offset,
                      router->binlog_name,
                      strerror_r(errno, err_msg, sizeof(err_msg)));
        }
        MXS_FREE(new_event);
        return 0;
    }

    MXS_FREE(new_event);

    // Increment offsets, next event will be written after this special one
    spinlock_acquire(&router->binlog_lock);

    router->last_written += event_size;
    router->current_pos = file_offset + event_size;
    router->last_event_pos = file_offset;

    spinlock_release(&router->binlog_lock);

    // Force write
    fsync(router->binlog_fd);

    return 1;
}

/** Create the START_ENCRYPTION_EVENT
 *
 * This is a New Event added in MariaDB 10.1.7
 * Type is 0xa4 and size 36 (crc32 not included)
 *
 * @param hdr            Current replication event header, received from master
 * @param event_pos      The position in binlog file of the new event
 * @param do_checksum    Whether checksum must be calculated and stored
 * @return               Returns the pointer of new event
 */

uint8_t *
blr_create_start_encryption_event(ROUTER_INSTANCE *router, uint32_t event_pos, bool do_checksum)
{
    uint8_t *new_event;
    uint8_t event_size = sizeof(START_ENCRYPTION_EVENT);
    BINLOG_ENCRYPTION_CTX *new_encryption_ctx = MXS_CALLOC(1, sizeof(BINLOG_ENCRYPTION_CTX));

    if (new_encryption_ctx == NULL)
    {
        return NULL;
    }

    /* Add 4 bytes to event size with crc32 */
    if (do_checksum)
    {
        event_size += BINLOG_EVENT_CRC_SIZE;
    }

    new_event = MXS_CALLOC(1, event_size);
    if (new_event == NULL)
    {
        return NULL;
    }

    // Populate Event header 19 bytes
    encode_value(&new_event[0], time(NULL), 32); // now
    new_event[4] = MARIADB10_START_ENCRYPTION_EVENT; // type is BEGIN_ENCRYPTION_EVENT
    /* Set binlog server instance server id */
    encode_value(&new_event[5], router->serverid, 32); // serverid of maxscale
    encode_value(&new_event[9], event_size, 32); // event size
    encode_value(&new_event[13], event_pos + event_size, 32); // next_pos
    encode_value(&new_event[17], 0, 16); // flag is 0 ?

    /**
     *  Now add the event content, after 19 bytes of header
     */

    /* Set the encryption schema, 1 byte: set to 1 */
    new_event[BINLOG_EVENT_HDR_LEN] = 1;
    /* The encryption key version, 4 bytes: set to 1, is added after previous one 1 byte */
    encode_value(&new_event[BINLOG_EVENT_HDR_LEN + 1], 1, 32);
    /* The nonce (12 random bytes) is added after previous 5 bytes */
    gw_generate_random_str((char *)&new_event[BINLOG_EVENT_HDR_LEN + 4 + 1], BLRM_NONCE_LENGTH);

    /* if checksum is requred add the crc32 */
    if (do_checksum)
    {
        /*
         * Now add the CRC to the Ignorable binlog event.
         *
         * The algorithm is first to compute the checksum of an empty buffer
         * and then the checksum of the event.
         */
        uint32_t chksum;
        chksum = crc32(0L, NULL, 0);
        chksum = crc32(chksum, new_event, event_size - BINLOG_EVENT_CRC_SIZE);

        // checksum is stored at the end of current event data: 4 less bytes than event size
        encode_value(new_event + event_size - BINLOG_EVENT_CRC_SIZE, chksum, 32);
    }

    /* Update the encryption context */
    uint8_t *nonce_ptr = &(new_event[BINLOG_EVENT_HDR_LEN + 4 + 1]);

    spinlock_acquire(&router->binlog_lock);

    memcpy(new_encryption_ctx->nonce, nonce_ptr, BLRM_NONCE_LENGTH);
    new_encryption_ctx->binlog_crypto_scheme = new_event[BINLOG_EVENT_HDR_LEN];
    memcpy(&new_encryption_ctx->binlog_key_version,
           &new_event[BINLOG_EVENT_HDR_LEN + 1], BLRM_KEY_VERSION_LENGTH);

    /* Set the router encryption context for current binlog file */
    MXS_FREE(router->encryption_ctx);
    router->encryption_ctx = new_encryption_ctx;

    spinlock_release(&router->binlog_lock);

    return new_event;
}

/**
 * Encrypt/Decrypt an array of bytes
 *
 * Note: The output buffer is 4 bytes larger than input
 * Encrypted bytes start at offset 4
 *
 * @param router    The router instance
 * @param buffer    The buffer to encrypt/decrypt
 * @param size      The buffer size
 * @param iv        The AES initialisation Vector
 * @action          Crypt action: 1 encrypt, 1 decrypt
 * @return          A new allocated, encrypted, GWBUF buffer
 *
 */
static GWBUF *blr_aes_crypt(ROUTER_INSTANCE *router,
                            uint8_t *buffer,
                            uint32_t size,
                            uint8_t *iv,
                            int action)
{
    uint8_t *key = router->encryption.key_value;
    unsigned int key_len = router->encryption.key_len;
    int outlen;
    int flen;
    uint32_t encrypted_size = size + 4;
    int total_len;
    GWBUF *outbuf;
    uint8_t *out_ptr;

    if (key_len == 0)
    {
        MXS_ERROR("The encrytion key len is 0");
        return NULL;
    }

    if ((outbuf = gwbuf_alloc(encrypted_size)) == NULL)
    {
        return NULL;
    }

    out_ptr = GWBUF_DATA(outbuf);

    EVP_CIPHER_CTX *ctx = mxs_evp_cipher_ctx_alloc();

    /* Set the encryption algorithm accordingly to key_len and encryption mode */
    if (!EVP_CipherInit_ex(ctx,
                           ciphers[router->encryption.encryption_algorithm](router->encryption.key_len),
                           NULL,
                           key,
                           iv,
                           action))
    {
        MXS_ERROR("Error in EVP_CipherInit_ex for algo %d", router->encryption.encryption_algorithm);
        mxs_evp_cipher_ctx_free(ctx);
        MXS_FREE(outbuf);
        return NULL;
    }

    /* Set no padding */
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    /* Encryt/Decrypt the input data */
    if (!EVP_CipherUpdate(ctx,
                          out_ptr + 4,
                          &outlen,
                          buffer,
                          size))
    {
        MXS_ERROR("Error in EVP_CipherUpdate");
        mxs_evp_cipher_ctx_free(ctx);
        MXS_FREE(outbuf);
        return NULL;
    }

    int finale_ret = 1;

    /* Enc/dec finish is differently handled for AES_CBC */
    if (router->encryption.encryption_algorithm != BLR_AES_CBC)
    {
        /* Call Final_ex */
        if (!EVP_CipherFinal_ex(ctx,
                                (out_ptr + 4 + outlen),
                                (int*)&flen))
        {
            MXS_ERROR("Error in EVP_CipherFinal_ex");
            finale_ret = 0;
        }
    }
    else
    {
        /**
         * If some bytes (ctx.buf_len) are still available in ctx.buf
         * handle them with ECB and XOR
         */
        if (size - outlen > 0)
        {
            if (!blr_aes_create_tail_for_cbc(out_ptr + 4 + outlen,
                                             mxs_evp_cipher_ctx_buf(ctx),
                                             size - outlen,
                                             mxs_evp_cipher_ctx_oiv(ctx),
                                             router->encryption.key_value,
                                             router->encryption.key_len))
            {
                MXS_ERROR("Error in blr_aes_create_tail_for_cbc");
                finale_ret = 0;
            }
        }
    }

    if (!finale_ret)
    {
        MXS_FREE(outbuf);
        outbuf = NULL;
    }

    mxs_evp_cipher_ctx_free(ctx);

    return outbuf;
}

/**
 * The routine prepares a binlg event for encryption and ecrypts it
 *
 * @param router    The ruter instance
 * @buf             The binlog event
 * @size            The event size (CRC32 four bytes included)
 * @pos             The position of the event in binlog file
 * @nonce           The binlog nonce 12 bytes as in START_ENCRYPTION_EVENT
 *                  of requested or current binlog file
 *                  If nonce is NULL the one from current binlog file is used.
 * @action          Encryption action: 1 Encryp, 0 Decryot
 * @return          A GWBUF buffer or NULL omn error
 */
static GWBUF *blr_prepare_encrypted_event(ROUTER_INSTANCE *router,
                                          uint8_t *buf,
                                          uint32_t size,
                                          uint32_t pos,
                                          const uint8_t *nonce,
                                          int action)
{
    uint8_t iv[BLRM_IV_LENGTH];
    uint32_t file_offset = pos;
    uint8_t event_size[4];
    const uint8_t *nonce_ptr = nonce;
    GWBUF *encrypted;
    uint8_t *enc_ptr;

    /* If nonce is NULL use the router current binlog file */
    if (nonce_ptr == NULL)
    {
        BINLOG_ENCRYPTION_CTX *encryption_ctx = (BINLOG_ENCRYPTION_CTX *)(router->encryption_ctx);
        nonce_ptr = encryption_ctx->nonce;
    }

    /* Encryption IV is 12 bytes nonce + 4 bytes event position */
    memcpy(iv, nonce_ptr, BLRM_NONCE_LENGTH);
    gw_mysql_set_byte4(iv + BLRM_NONCE_LENGTH, (unsigned long)file_offset);

    /**
     * Encrypt binlog event, steps:
     *
     * 1: Save event size (buf + 9, 4 bytes)
     * 2: move first 4 bytes of buf to buf + 9
     * 3: encrypt buf starting from buf + 4 (so it will be event_size - 4)
     * 4: move encrypted_data + 9 (4 bytes) to  encrypted_data[0]
     * 5: Copy saved_event_size 4 bytes into encrypted_data + 9
     */

    /* (1): Save event size (buf + 9, 4 bytes) */
    memcpy(&event_size, buf + BINLOG_EVENT_LEN_OFFSET, 4);
    /* (2): move first 4 bytes of buf to buf + 9 */
    memmove(buf + BINLOG_EVENT_LEN_OFFSET, buf, 4);

#ifdef SS_DEBUG
    char iv_hex[AES_BLOCK_SIZE * 2 + 1] = "";
    char nonce_hex[BLRM_NONCE_LENGTH * 2 + 1] = "";

    /* Human readable debug */
    gw_bin2hex(iv_hex, iv, BLRM_IV_LENGTH);
    gw_bin2hex(nonce_hex, nonce_ptr, BLRM_NONCE_LENGTH);

    MXS_DEBUG("** Encryption/Decryption of Event @ %lu: the IV is %s, size is %lu, next pos is %lu",
              (unsigned long)pos,
              iv_hex, (unsigned long)size,
              (unsigned long)(pos + size));
#endif

    /**
     * (3): encrypt the event stored in buf starting from (buf + 4):
     * with len (event_size - 4)
     *
     * NOTE: the encrypted_data buffer returned by blr_aes_encrypt() contains:
     * (size - 4) encrypted bytes + (4) bytes event size in clear
     *
     * The encrypted buffer has same size of the original event (size variable)
     */

    if ((encrypted = blr_aes_crypt(router, buf + 4, size - 4, iv, action)) == NULL)
    {
        return NULL;
    }

    enc_ptr = GWBUF_DATA(encrypted);

    /* (4): move encrypted_data + 9 (4 bytes) to  encrypted_data[0] */
    memmove(enc_ptr, enc_ptr + BINLOG_EVENT_LEN_OFFSET, 4);

    /* (5): Copy saved_event_size 4 bytes into encrypted_data + 9 */
    memcpy(enc_ptr + BINLOG_EVENT_LEN_OFFSET, &event_size, 4);

    return encrypted;
}

/**
 * Return the encryption algorithm string
 *
 * @param algo    The algorithm value
 * @return        A static string or NULL
 */
const char *blr_get_encryption_algorithm(int algo)
{
    if (algo < 0 || algo >= BINLOG_MAX_CRYPTO_SCHEME)
    {
        return NULL;
    }
    else
    {
        return blr_encryption_algorithm_names[algo];
    }
}

/**
 * Return the encryption algorithm value
 *
 * @param name   The alogorithm string
 * @return       The numeric value or -1 on error
 */
int blr_check_encryption_algorithm(char *name)
{
    if (name)
    {
        if (strcasecmp(name, "aes_cbc") == 0)
        {
            return BLR_AES_CBC;
        }
#if OPENSSL_VERSION_NUMBER > 0x10000000L
        if (strcasecmp(name, "aes_ctr") == 0)
        {
            return BLR_AES_CTR;
        }
#endif
    }

    return -1;
}

/**
 * Return a string with a list of supported algorithms
 *
 * @return    The algorith list as char *
 */
const char *blr_encryption_algorithm_list(void)
{
    return blr_encryption_algorithm_list_names;
}

/**
 * Creates the final buffer for AES_CBC encryption
 *
 * As the encrypted/decrypted data must have same size of inpu data
 * the remaining data from EVP_CipherUpdate with AES_CBC engine
 * are handled this way:
 *
 * 1) The IV in the previous stage is encrypted with AES_ECB
 *    using the key and a NULL iv
 * 2) the remaing data from previous stage are XORed with thant buffer
 *    and the the ouput buffer contains the result
 *
 * @param output    The outut buffer to fill
 * @param input     The input buffere 8remaining bytes from previous stage)
 * @param in_size   The inout data size
 * @param iv        The IV used in previous stage
 * @param key       The encryption key
 * @param key_len   The lenght of encrytion key
 * @return          Return 1 on success, 0 otherwise
 */
static int blr_aes_create_tail_for_cbc(uint8_t *output,
                                       uint8_t *input,
                                       uint32_t in_size,
                                       uint8_t *iv,
                                       uint8_t *key,
                                       unsigned int key_len)
{
    uint8_t mask[AES_BLOCK_SIZE];
    int mlen = 0;

    EVP_CIPHER_CTX* t_ctx = mxs_evp_cipher_ctx_alloc();

    /* Initialise with AES_ECB and NULL iv */
    if (!EVP_CipherInit_ex(t_ctx,
                           ciphers[BLR_AES_ECB](key_len),
                           NULL,
                           key,
                           NULL, /* NULL iv */
                           BINLOG_FLAG_ENCRYPT))
    {
        MXS_ERROR("Error in EVP_CipherInit_ex CBC for last block (ECB)");
        mxs_evp_cipher_ctx_free(t_ctx);
        return 0;
    }

    /* Set no padding */
    EVP_CIPHER_CTX_set_padding(t_ctx, 0);

    /* Do the enc/dec of the IV (the one from previous stage) */
    if (!EVP_CipherUpdate(t_ctx,
                          mask,
                          &mlen,
                          iv,
                          sizeof(mask)))
    {
        MXS_ERROR("Error in EVP_CipherUpdate ECB");
        mxs_evp_cipher_ctx_free(t_ctx);
        return 0;
    }

    /**
     * Now the output buffer contains
     * the XORed data of input data and the mask (encryption of IV)
     *
     * Note: this also works for decryption
     */
    for (int i = 0; i < in_size; i++)
    {
        output[i] = input[i] ^ mask[i];
    }

    mxs_evp_cipher_ctx_free(t_ctx);

    return 1;
}

/**
 * Run checks against some fieds in replication header
 *
 * @param router        The router instance
 * @param pos           The current pos in binlog
 * @param hdr           The replication header struct
 * @param binlogname    The binlogname, for error message
 * @param errmsg        The errormessage to fill
 * @return              0 on error and 1 on success
 *
 * 1 ok, 0 err */
static int blr_binlog_event_check(ROUTER_INSTANCE *router,
                                  unsigned long pos,
                                  REP_HEADER *hdr,
                                  char *binlogname,
                                  char *errmsg)
{
    /* event pos & size checks */
    if (hdr->event_size == 0 || ((hdr->next_pos != (pos + hdr->event_size)) &&
                                 (hdr->event_type != ROTATE_EVENT)))
    {
        snprintf(errmsg, BINLOG_ERROR_MSG_LEN,
                 "Client requested master to start replication from invalid "
                 "position %lu in binlog file '%s'", pos,
                 binlogname);
        return 0;
    }

    /* event type checks */
    if (router->mariadb10_compat)
    {
        if (hdr->event_type > MAX_EVENT_TYPE_MARIADB10)
        {
            snprintf(errmsg, BINLOG_ERROR_MSG_LEN,
                     "Invalid MariaDB 10 event type 0x%x at %lu in binlog file '%s'",
                     hdr->event_type, pos, binlogname);
            return 0;
        }
    }
    else
    {
        if (hdr->event_type > MAX_EVENT_TYPE)
        {
            snprintf(errmsg, BINLOG_ERROR_MSG_LEN,
                     "Invalid event type 0x%x at %lu in binlog file '%s'", hdr->event_type,
                     pos, binlogname);
            return 0;
        }
    }

    /* check is OK */
    return 1;
}

/**
 * Fill a string buffer with HEX representation of CRC32 (4) bytes
 * at the end of binlog event
 *
 * @param hdr    The replication header struct
 * @param buffer The buffer with binlog event
 * @output       The output buffer to fill, preallocated by the caller
 */
static void blr_report_checksum(REP_HEADER hdr, const uint8_t *buffer, char *output)
{
    uint8_t cksum_data[BINLOG_EVENT_CRC_SIZE];
    char *ptr = output + strlen(BLR_REPORT_CHECKSUM_FORMAT);
    strcpy(output, BLR_REPORT_CHECKSUM_FORMAT);

    /* Hex representation of checksum */
    cksum_data[3] = *(buffer + hdr.event_size - 4 - BINLOG_EVENT_HDR_LEN);
    cksum_data[2] = *(buffer + hdr.event_size - 3 - BINLOG_EVENT_HDR_LEN);
    cksum_data[1] = *(buffer + hdr.event_size - 2 - BINLOG_EVENT_HDR_LEN);
    cksum_data[0] = *(buffer + hdr.event_size - 1 - BINLOG_EVENT_HDR_LEN);

    gw_bin2hex(ptr, cksum_data, BINLOG_EVENT_CRC_SIZE);
    for (char *p = ptr ; *p; ++p)
    {
        *p = tolower(*p);
    }
}

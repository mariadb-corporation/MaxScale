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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <syslog.h>
#include <atomic.h>

#include <skygw_debug.h>
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

#define MAX_PREFIXLEN 250
#define MAX_SUFFIXLEN 250
#define MAX_PATHLEN   512
#define MAXNBLOCKBUFS 10

/** for procname */
#if !defined(_GNU_SOURCE)
# define _GNU_SOURCE
#endif

static const char LOGFILE_NAME_PREFIX[] = "maxscale";
static const char LOGFILE_NAME_SUFFIX[] = ".log";

extern char *program_invocation_name;
extern char *program_invocation_short_name;

typedef enum
{
    BB_READY = 0x00,
    BB_FULL,
    BB_CLEARED
} blockbuf_state_t;

typedef enum
{
    FILEWRITER_INIT,
    FILEWRITER_RUN,
    FILEWRITER_DONE
} filewriter_state_t;

/**
 * UNINIT means zeroed memory buffer allocated for the struct.
 * INIT   means that struct members may have values, and memory may
 *        have been allocated. Done function must check and free it.
 * RUN    Struct is valid for run-time checking.
 * DONE   means that possible memory allocations have been released.
 */
typedef enum
{
    UNINIT = 0,
    INIT,
    RUN,
    DONE
} flat_obj_state_t;

/**
 * LOG_FLUSH_NO  Do not flush after writing.
 * LOG_FLUSH_YES Flush after writing.
 */
enum log_flush
{
    LOG_FLUSH_NO  = 0,
    LOG_FLUSH_YES = 1
};

#if defined(SS_DEBUG)
static int write_index;
static int block_start_index;
static int prevval;
static simple_mutex_t msg_mutex;
#endif

/**
 * Default augmentation.
 */
static int DEFAULT_LOG_AUGMENTATION = 0;

static struct
{
    int  augmentation;     // Can change during the lifetime of log_manager.
    bool do_highprecision; // Can change during the lifetime of log_manager.
    bool do_syslog;        // Can change during the lifetime of log_manager.
    bool do_maxlog;        // Can change during the lifetime of log_manager.
    bool use_stdout;       // Can NOT changed during the lifetime of log_manager.
} log_config =
{
    DEFAULT_LOG_AUGMENTATION, // augmentation
    false,                    // do_highprecision
    true,                     // do_syslog
    true,                     // do_maxlog
    false                     // use_stdout
};

/**
 * Variable holding the enabled priorities information.
 * Used from logging macros.
 */
int mxs_log_enabled_priorities = 0;

/**
 * Thread-specific struct variable for storing current session id and currently
 * enabled log files for the session.
 */
__thread mxs_log_info_t mxs_log_tls = {0, 0};

/**
 * Global counter for each log file type. It indicates for how many sessions
 * each log type is currently enabled.
 */
ssize_t mxs_log_session_count[LOG_DEBUG + 1] = {0};

/**
 * BUFSIZ comes from the system. It equals with block size or
 * its multiplication.
 */
#define MAX_LOGSTRLEN BUFSIZ

/**
 * Path to directory in which all files are stored to shared memory
 * by the OS.
 */
const char* shm_pathname_prefix = "/dev/shm/";

/** Forward declarations
 */
typedef struct filewriter  filewriter_t;
typedef struct logfile     logfile_t;
typedef struct fnames_conf fnames_conf_t;
typedef struct logmanager  logmanager_t;

/**
 * Global log manager pointer and lock variable.
 * lmlock protects logmanager access.
 */
static int lmlock;
static logmanager_t* lm;
static bool flushall_flag;
static bool flushall_started_flag;
static bool flushall_done_flag;

/** This is used to detect if the initialization of the log manager has failed
 * and that it isn't initialized again after a failure has occurred. */
static bool fatal_error = false;

/** Writer thread structure */
struct filewriter
{
#if defined(SS_DEBUG)
    skygw_chk_t        fwr_chk_top;
#endif
    flat_obj_state_t   fwr_state;
    logmanager_t*      fwr_logmgr;
    /** Physical files */
    skygw_file_t*      fwr_file;
    /** fwr_logmes is for messages from log clients */
    skygw_message_t*   fwr_logmes;
    /** fwr_clientmes is for messages to log clients */
    skygw_message_t*   fwr_clientmes;
    skygw_thread_t*    fwr_thread;
#if defined(SS_DEBUG)
    skygw_chk_t        fwr_chk_tail;
#endif
};

/**
 * Log client's string is copied to block-sized log buffer, which is passed
 * to file writer thread.
 */
typedef struct blockbuf
{
#if defined(SS_DEBUG)
    skygw_chk_t    bb_chk_top;
#endif
    blockbuf_state_t bb_state; /**State of the block buffer*/
    simple_mutex_t bb_mutex;    /**< bb_buf_used, bb_isfull */
    int            bb_refcount; /**< protected by list mutex. #of clients */
//        int            bb_blankcount; /**< # of blanks used btw strings */
    size_t         bb_buf_size;
    size_t         bb_buf_left;
    size_t         bb_buf_used;
    char           bb_buf[MAX_LOGSTRLEN];
#if defined(SS_DEBUG)
    skygw_chk_t    bb_chk_tail;
#endif
} blockbuf_t;

/**
 * logfile object corresponds to physical file(s) where
 * certain log is written.
 */
struct logfile
{
#if defined(SS_DEBUG)
    skygw_chk_t      lf_chk_top;
#endif
    flat_obj_state_t lf_state;
    bool             lf_init_started;
    bool             lf_store_shmem;
    logmanager_t*    lf_lmgr;
    /** fwr_logmes is for messages from log clients */
    skygw_message_t* lf_logmes;
    char*            lf_filepath; /**< path to file used for logging */
    char*            lf_linkpath; /**< path to symlink file.  */
    const char*      lf_name_prefix;
    const char*      lf_name_suffix;
    int              lf_name_seqno;
    char*            lf_full_file_name; /**< complete log file name */
    char*            lf_full_link_name; /**< complete symlink name */
    int              lf_nfiles_max;
    size_t           lf_file_size;
    /** list of block-sized log buffers */
    mlist_t          lf_blockbuf_list;
    size_t           lf_buf_size;
    bool             lf_flushflag;
    bool                 lf_rotateflag;
    int              lf_spinlock; /**< lf_flushflag & lf_rotateflag */
    int              lf_npending_writes;
#if defined(SS_DEBUG)
    skygw_chk_t      lf_chk_tail;
#endif
};


struct fnames_conf
{
#if defined(SS_DEBUG)
    skygw_chk_t      fn_chk_top;
#endif
    flat_obj_state_t fn_state;
    char*            fn_logpath;
#if defined(SS_DEBUG)
    skygw_chk_t      fn_chk_tail;
#endif
};

struct logmanager
{
#if defined(SS_DEBUG)
    skygw_chk_t      lm_chk_top;
#endif
    bool             lm_enabled;
    simple_mutex_t   lm_mutex;
    size_t           lm_nlinks;
    /** fwr_logmes is for messages from log clients */
    skygw_message_t* lm_logmes;
    /** fwr_clientmes is for messages to log clients */
    skygw_message_t* lm_clientmes;
    fnames_conf_t    lm_fnames_conf;
    logfile_t        lm_logfile;
    filewriter_t     lm_filewriter;
    mxs_log_target_t lm_target;
#if defined(SS_DEBUG)
    skygw_chk_t      lm_chk_tail;
#endif
};

/**
 * Type definition for string part. It is used in forming the log file name
 * from string parts provided by the client of log manager, as arguments.
 */

typedef struct strpart
{
    const char*     sp_string;
    struct strpart* sp_next;
} strpart_t;


/** Static function declarations */
static bool logfiles_init(logmanager_t* lmgr);
static bool logfile_init(logfile_t*     logfile,
                         logmanager_t*  logmanager,
                         bool           store_shmem);
static void logfile_done(logfile_t* logfile);
static void logfile_free_memory(logfile_t* lf);
static void logfile_flush(logfile_t* lf);
static void logfile_rotate(logfile_t* lf);
static bool logfile_build_name(logfile_t* lf);
static bool logfile_open_file(filewriter_t* fw, logfile_t* lf);
static char* form_full_file_name(strpart_t* parts, logfile_t* lf, int seqnoidx);

static bool filewriter_init(logmanager_t* logmanager,
                            filewriter_t* fw);
static void filewriter_done(filewriter_t* filewriter);
static bool fnames_conf_init(fnames_conf_t* fn, const char* logdir);
static void fnames_conf_done(fnames_conf_t* fn);
static void fnames_conf_free_memory(fnames_conf_t* fn);
static void* thr_filewriter_fun(void* data);
static logfile_t* logmanager_get_logfile(logmanager_t* lm);
static bool logmanager_register(bool writep);
static void logmanager_unregister(void);
static bool logmanager_init_nomutex(const char* ident,
                                    const char* logdir,
                                    mxs_log_target_t target);
static void logmanager_done_nomutex(void);

static int logmanager_write_log(int            priority,
                                enum log_flush flush,
                                size_t         prefix_len,
                                size_t         len,
                                const char*    str);

static blockbuf_t* blockbuf_init();
static void blockbuf_node_done(void* bb_data);
static char* blockbuf_get_writepos(blockbuf_t** p_bb,
                                   size_t       str_len,
                                   bool         flush);

static void blockbuf_register(blockbuf_t* bb);
static void blockbuf_unregister(blockbuf_t* bb);
static char* add_slash(char* str);

static bool check_file_and_path(char* filename,
                                bool* writable,
                                bool  do_log);

static bool file_is_symlink(char* filename);
static int find_last_seqno(strpart_t* parts, int seqno, int seqnoidx);
void flushall_logfiles(bool flush);
bool thr_flushall_check();

static bool logmanager_init_nomutex(const char* ident,
                                    const char* logdir,
                                    mxs_log_target_t target)
{
    fnames_conf_t* fn;
    filewriter_t*  fw;
    int            err;
    bool           succ = false;

    lm = (logmanager_t *)calloc(1, sizeof(logmanager_t));

    if (lm == NULL)
    {
        err = 1;
        goto return_succ;
    }

    lm->lm_target = (target == MXS_LOG_TARGET_DEFAULT ? MXS_LOG_TARGET_FS : target);
#if defined(SS_DEBUG)
    lm->lm_chk_top   = CHK_NUM_LOGMANAGER;
    lm->lm_chk_tail  = CHK_NUM_LOGMANAGER;
    write_index = 0;
    block_start_index = 0;
    prevval = -1;
    simple_mutex_init(&msg_mutex, "Message mutex");
#endif
    lm->lm_clientmes = skygw_message_init();
    lm->lm_logmes    = skygw_message_init();

    if (lm->lm_clientmes == NULL || lm->lm_logmes == NULL)
    {
        err = 1;
        goto return_succ;
    }

    fn = &lm->lm_fnames_conf;
    fw = &lm->lm_filewriter;
    fn->fn_state  = UNINIT;
    fw->fwr_state = UNINIT;

    // The openlog call is always made, but a connection to the system logger will
    // not be made until a call to syslog is made.
    openlog(ident, LOG_PID | LOG_ODELAY, LOG_USER);

    /** Initialize configuration including log file naming info */
    if (!fnames_conf_init(fn, logdir))
    {
        err = 1;
        goto return_succ;
    }

    /** Initialize logfiles */
    if (!logfiles_init(lm))
    {
        err = 1;
        goto return_succ;
    }

    /**
     * Set global variable
     */
    mxs_log_enabled_priorities = MXS_LOG_ERR | MXS_LOG_NOTICE | MXS_LOG_WARNING;

    /**
     * Initialize filewriter data and open the log file
     * for each log file type.
     */
    if (!filewriter_init(lm, fw))
    {
        err = 1;
        goto return_succ;
    }

    /** Initialize and start filewriter thread */
    fw->fwr_thread = skygw_thread_init("filewriter thr", thr_filewriter_fun, (void *)fw);

    if (fw->fwr_thread == NULL)
    {
        err = 1;
        goto return_succ;
    }

    if ((err = skygw_thread_start(fw->fwr_thread)) != 0)
    {
        goto return_succ;
    }
    /** Wait message from filewriter_thr */
    skygw_message_wait(fw->fwr_clientmes);

    succ = true;
    lm->lm_enabled = true;

return_succ:
    if (err != 0)
    {
        /** This releases memory of all created objects */
        logmanager_done_nomutex();
        fprintf(stderr, "*\n* Error : Initializing log manager failed.\n*\n");
    }
    return succ;
}



/**
 * Initializes log managing routines in MariaDB Corporation MaxScale.
 *
 * @param ident  The syslog ident. If NULL, then the program name is used.
 * @param logdir The directory for the log file. If NULL logging will be made to stdout.
 * @param target Whether the log should be written to filesystem or shared memory.
 *               Meaningless if logdir is NULL.
 *
 * @return true if succeed, otherwise false
 *
 */
bool mxs_log_init(const char* ident, const char* logdir, mxs_log_target_t target)
{
    bool succ = false;

    acquire_lock(&lmlock);

    if (!lm)
    {
        succ = logmanager_init_nomutex(ident, logdir, target);
    }
    else
    {
        // TODO: This is not ok. If the parameters are different then
        // TODO: we pretend something is what it is not.
        succ = true;
    }

    release_lock(&lmlock);

    return succ;
}

/**
 * Release resources of log manager.
 *
 * Lock must have been acquired before calling
 * this function.
 */
static void logmanager_done_nomutex(void)
{
    int           i;
    logfile_t*    lf;
    filewriter_t* fwr;

    fwr = &lm->lm_filewriter;

    if (fwr->fwr_state == RUN)
    {
        CHK_FILEWRITER(fwr);
        /** Inform filewriter thread and wait until it has stopped. */
        skygw_thread_set_exitflag(fwr->fwr_thread, fwr->fwr_logmes, fwr->fwr_clientmes);

        /** Free thread memory */
        skygw_thread_done(fwr->fwr_thread);
    }

    /** Free filewriter memory. */
    filewriter_done(fwr);

    lf = logmanager_get_logfile(lm);
    /** Release logfile memory */
    logfile_done(lf);

    closelog();

    /** Release messages and finally logmanager memory */
    fnames_conf_done(&lm->lm_fnames_conf);
    skygw_message_done(lm->lm_clientmes);
    skygw_message_done(lm->lm_logmes);

    /** Set global pointer NULL to prevent access to freed data. */
    free(lm);
    lm = NULL;
}

/**
 * End execution of log manager
 *
 * Stops file writing thread, releases filewriter, and logfiles.
 *
 */
void mxs_log_finish(void)
{
    acquire_lock(&lmlock);

    if (lm)
    {
        CHK_LOGMANAGER(lm);
        /** Mark logmanager unavailable */
        lm->lm_enabled = false;

        /** Wait until all users have left or someone shuts down
         * logmanager between lock release and acquire.
         */
        while (lm != NULL && lm->lm_nlinks != 0)
        {
            release_lock(&lmlock);
            pthread_yield();
            acquire_lock(&lmlock);
        }

        /** Shut down if not already shutted down. */
        if (lm)
        {
            ss_dassert(lm->lm_nlinks == 0);
            logmanager_done_nomutex();
        }
    }

    release_lock(&lmlock);
}

static logfile_t* logmanager_get_logfile(logmanager_t* lmgr)
{
    logfile_t* lf;
    CHK_LOGMANAGER(lmgr);
    lf = &lmgr->lm_logfile;

    if (lf->lf_state == RUN)
    {
        CHK_LOGFILE(lf);
    }
    return lf;
}

/**
 * Finds write position from block buffer for log string and writes there.
 *
 * Parameters:
 *
 * @param priority      Syslog priority
 * @param flush         indicates whether log string must be written to disk
 *                      immediately
 * @param rotate        if set, closes currently open log file and opens a
 *                      new one
 * @param prefix_len    length of prefix to be stripped away when syslogging
 * @param str_len       length of formatted string (including terminating NULL).
 * @param str           string to be written to log
 *
 * @return 0 if succeed, -1 otherwise
 *
 */
static int logmanager_write_log(int            priority,
                                enum log_flush flush,
                                size_t         prefix_len,
                                size_t         str_len,
                                const char*    str)
{
    logfile_t*   lf;
    char*        wp;
    int          err = 0;
    blockbuf_t*  bb;
    blockbuf_t*  bb_c;
    size_t       timestamp_len;
    int          i;

    // The config parameters are copied to local variables, because the values in
    // log_config may change during the course of the function, with would have
    // unpleasant side-effects.
    int do_highprecision = log_config.do_highprecision;
    int do_maxlog = log_config.do_maxlog;
    int do_syslog = log_config.do_syslog;

    assert(str);
    assert((priority & ~LOG_PRIMASK) == 0);
    CHK_LOGMANAGER(lm);

    // All messages are now logged to the error log file.
    lf = &lm->lm_logfile;
    CHK_LOGFILE(lf);

    /** Length of string that will be written, limited by bufsize */
    size_t safe_str_len;
    /** Length of session id */
    size_t sesid_str_len;
    size_t cmplen = 0;
    /**
     * 2 braces, 2 spaces and terminating char
     * If session id is stored to mxs_log_tls structure, allocate
     * room for session id too.
     */
    if ((priority == LOG_INFO) && (mxs_log_tls.li_sesid != 0))
    {
        sesid_str_len = 5 * sizeof(char) + get_decimal_len(mxs_log_tls.li_sesid);
    }
    else
    {
        sesid_str_len = 0;
    }
    if (do_highprecision)
    {
        timestamp_len = get_timestamp_len_hp();
    }
    else
    {
        timestamp_len = get_timestamp_len();
    }
    cmplen = sesid_str_len > 0 ? sesid_str_len - sizeof(char) : 0;

    bool overflow = false;
    /** Find out how much can be safely written with current block size */
    if (timestamp_len - sizeof(char) + cmplen + str_len > lf->lf_buf_size)
    {
        safe_str_len = lf->lf_buf_size;
        overflow = true;
    }
    else
    {
        safe_str_len = timestamp_len - sizeof(char) + cmplen + str_len;
    }
    /**
     * Seek write position and register to block buffer.
     * Then print formatted string to write position.
     */

#if defined (SS_LOG_DEBUG)
    {
        char *copy, *tok;
        int tokval;

        simple_mutex_lock(&msg_mutex, true);
        copy = strdup(str);
        tok = strtok(copy, "|");
        tok = strtok(NULL, "|");

        if (strstr(str, "message|") && tok)
        {
            tokval = atoi(tok);

            if (prevval > 0)
            {
                ss_dassert(tokval == (prevval + 1));
            }
            prevval = tokval;
        }
        free(copy);
        simple_mutex_unlock(&msg_mutex);
    }
#endif
    /** Book space for log string from buffer */
    if (do_maxlog)
    {
        // All messages are now logged to the error log file.
        wp = blockbuf_get_writepos(&bb, safe_str_len, flush);
    }
    else
    {
        wp = (char*)malloc(sizeof(char) * (timestamp_len - sizeof(char) + cmplen + str_len + 1));
    }

    if (wp == NULL)
    {
        return -1;
    }

#if defined (SS_LOG_DEBUG)
    {
        sprintf(wp, "[msg:%d]", atomic_add(&write_index, 1));
        safe_str_len -= strlen(wp);
        wp += strlen(wp);
    }
#endif
    /**
     * Write timestamp with at most <timestamp_len> characters
     * to wp.
     * Returned timestamp_len doesn't include terminating null.
     */
    if (do_highprecision)
    {
        timestamp_len = snprint_timestamp_hp(wp, timestamp_len);
    }
    else
    {
        timestamp_len = snprint_timestamp(wp, timestamp_len);
    }
    if (sesid_str_len != 0)
    {
        /**
         * Write session id
         */
        snprintf(wp + timestamp_len, sesid_str_len, "[%lu]  ", mxs_log_tls.li_sesid);
        sesid_str_len -= 1; /*< don't calculate terminating char anymore */
    }
    /**
     * Write next string to overwrite terminating null character
     * of the timestamp string.
     */
    snprintf(wp + timestamp_len + sesid_str_len,
             safe_str_len-timestamp_len-sesid_str_len,
             "%s",
             str);

    /** Add an ellipsis to an overflowing message to signal truncation. */
    if (overflow && safe_str_len > 4)
    {
        memset(wp + safe_str_len - 4, '.', 3);
    }
    /** write to syslog */
    if (do_syslog)
    {
        // Strip away the timestamp and the prefix (e.g. "error : ").
        const char *message = wp + timestamp_len + prefix_len;

        switch (priority)
        {
        case LOG_EMERG:
        case LOG_ALERT:
        case LOG_CRIT:
        case LOG_ERR:
        case LOG_WARNING:
        case LOG_NOTICE:
            syslog(priority, "%s", message);
            break;

        default:
            // LOG_INFO and LOG_DEBUG messages are never written to syslog.
            break;
        }
    }
    /** remove double line feed */
    if (wp[safe_str_len - 2] == '\n')
    {
        wp[safe_str_len - 2] = ' ';
    }
    wp[safe_str_len - 1] = '\n';

    if (do_maxlog)
    {
        blockbuf_unregister(bb);
    }
    else
    {
        free(wp);
    }

    return err;
}

/**
 * Register writer to a block buffer. When reference counter is non-zero the
 * flusher thread doesn't write the block to disk.
 *
 * @param bb    block buffer
 */
static void blockbuf_register(blockbuf_t* bb)
{
    CHK_BLOCKBUF(bb);
    ss_dassert(bb->bb_refcount >= 0);
    atomic_add(&bb->bb_refcount, 1);
}

/**
 * Unregister writer from block buffer. If the buffer got filled up and there
 * are no other registered writers anymore, notify the flusher thread.
 *
 * @param bb    block buffer
 */
static void blockbuf_unregister(blockbuf_t* bb)
{
    logfile_t* lf;

    CHK_BLOCKBUF(bb);
    ss_dassert(bb->bb_refcount >= 1);
    lf = &lm->lm_logfile;
    CHK_LOGFILE(lf);
    /**
     * if this is the last client in a full buffer, send write request.
     */
    if (atomic_add(&bb->bb_refcount, -1) == 1 && bb->bb_state == BB_FULL)
    {
        skygw_message_send(lf->lf_logmes);
    }
    ss_dassert(bb->bb_refcount >= 0);
}


/**
 * @node (write brief function description here)
 *
 * Parameters:
 * @param id - <usage>
 *          <description>
 *
 * @param str_len - <usage>
 *          <description>
 *
 * @return
 *
 *
 * @details List mutex now protects both the list and the contents of it.
 * TODO : It should be so that adding and removing nodes of the list is protected
 * by the list mutex. Buffer modifications should be protected by buffer
 * mutex.
 *
 */
static char* blockbuf_get_writepos(blockbuf_t** p_bb,
                                   size_t       str_len,
                                   bool         flush)
{
    logfile_t*     lf;
    mlist_t*       bb_list;
    char*          pos = NULL;
    mlist_node_t*  node;
    blockbuf_t*    bb;
#if defined(SS_DEBUG)
    bool           succ;
#endif

    CHK_LOGMANAGER(lm);
    lf = &lm->lm_logfile;
    CHK_LOGFILE(lf);
    bb_list = &lf->lf_blockbuf_list;

    /** Lock list */
    simple_mutex_lock(&bb_list->mlist_mutex, true);
    CHK_MLIST(bb_list);

    if (bb_list->mlist_nodecount > 0)
    {
        /**
         * At least block buffer exists on the list.
         */
        node = bb_list->mlist_first;

        /** Loop over blockbuf list to find write position */
        while (true)
        {
            CHK_MLIST_NODE(node);

            /** Unlock list */
            simple_mutex_unlock(&bb_list->mlist_mutex);

            bb = (blockbuf_t *)node->mlnode_data;
            CHK_BLOCKBUF(bb);

            /** Lock buffer */
            simple_mutex_lock(&bb->bb_mutex, true);

            if (bb->bb_state == BB_FULL || bb->bb_buf_left < str_len)
            {
                /**
                 * This block buffer is too full.
                 * Send flush request to file writer thread. This causes
                 * flushing all buffers, and (eventually) frees buffer space.
                 */
                blockbuf_register(bb);

                bb->bb_state = BB_FULL;

                blockbuf_unregister(bb);

                /** Unlock buffer */
                simple_mutex_unlock(&bb->bb_mutex);

                /** Lock list */
                simple_mutex_lock(&bb_list->mlist_mutex, true);


                /**
                 * If next node exists move forward. Else check if there is
                 * space for a new block buffer on the list.
                 */
                if (node != bb_list->mlist_last)
                {
                    node = node->mlnode_next;
                    continue;
                }
                /**
                 * All buffers on the list are full.
                 */
                if (bb_list->mlist_nodecount < bb_list->mlist_nodecount_max)
                {
                    /**
                     * New node is created
                     */
                    if ((bb = blockbuf_init()) == NULL)
                    {
                        simple_mutex_unlock(&bb_list->mlist_mutex);
                        return NULL;
                    }

                    CHK_BLOCKBUF(bb);

                    /**
                     * Increase version to odd to mark list update active
                     * update.
                     */
                    bb_list->mlist_versno += 1;
                    ss_dassert(bb_list->mlist_versno % 2 == 1);

                    ss_debug(succ =) mlist_add_data_nomutex(bb_list, bb);
                    ss_dassert(succ);

                    /**
                     * Increase version to even to mark completion of update.
                     */
                    bb_list->mlist_versno += 1;
                    ss_dassert(bb_list->mlist_versno % 2 == 0);
                }
                else
                {
                    /**
                     * List and buffers are full.
                     * Reset to the beginning of the list, and wait until
                     * there is a block buffer with enough space.
                     */
                    simple_mutex_unlock(&bb_list->mlist_mutex);
                    simple_mutex_lock(&bb_list->mlist_mutex, true);

                    node = bb_list->mlist_first;
                    continue;
                }

            }
            else if (bb->bb_state == BB_CLEARED)
            {

                /**
                 *Move the cleared buffer to the end of the list if it is the first one in the list
                 */

                simple_mutex_unlock(&bb->bb_mutex);
                simple_mutex_lock(&bb_list->mlist_mutex, true);

                if (node == bb_list->mlist_first)
                {

                    if ((bb_list->mlist_nodecount > 1) && (node != bb_list->mlist_last))
                    {
                        bb_list->mlist_last->mlnode_next = bb_list->mlist_first;
                        bb_list->mlist_first = bb_list->mlist_first->mlnode_next;
                        bb_list->mlist_last->mlnode_next->mlnode_next = NULL;
                        bb_list->mlist_last = bb_list->mlist_last->mlnode_next;
                    }

                    ss_dassert(node == bb_list->mlist_last);

                    simple_mutex_unlock(&bb_list->mlist_mutex);
                    simple_mutex_lock(&bb->bb_mutex, true);

                    bb->bb_state = BB_READY;

                    simple_mutex_unlock(&bb->bb_mutex);
                    simple_mutex_lock(&bb_list->mlist_mutex, true);
                    node = bb_list->mlist_first;
                }
                else
                {
                    if (node->mlnode_next)
                    {
                        node = node->mlnode_next;
                    }
                    else
                    {
                        node = bb_list->mlist_first;
                    }
                    continue;
                }

            }
            else if (bb->bb_state == BB_READY)
            {
                /**
                 * There is space for new log string.
                 */
                break;
            }
        } /** while (true) */
    }
    else
    {

        /**
         * Create the first block buffer to logfile's blockbuf list.
         */

        if ((bb = blockbuf_init()) == NULL)
        {
            return NULL;
        }

        CHK_BLOCKBUF(bb);

        /** Lock buffer */
        simple_mutex_lock(&bb->bb_mutex, true);
        /**
         * Increase version to odd to mark list update active update.
         */
        bb_list->mlist_versno += 1;
        ss_dassert(bb_list->mlist_versno % 2 == 1);

        ss_debug(succ =) mlist_add_data_nomutex(bb_list, bb);
        ss_dassert(succ);

        /**
         * Increase version to even to mark completion of update.
         */
        bb_list->mlist_versno += 1;
        ss_dassert(bb_list->mlist_versno % 2 == 0);

        /** Unlock list */
        simple_mutex_unlock(&bb_list->mlist_mutex);
    } /* if (bb_list->mlist_nodecount > 0) */

    ss_dassert(pos == NULL);
    ss_dassert(!(bb->bb_state == BB_FULL || bb->bb_buf_left < str_len));
    ss_dassert(bb_list->mlist_nodecount <= bb_list->mlist_nodecount_max);

    /**
     * Registration to blockbuf adds reference for the write operation.
     * It indicates that client has allocated space from the buffer,
     * but not written yet. As long as refcount > 0 buffer can't be
     * written to disk.
     */
    blockbuf_register(bb);
    *p_bb = bb;
    /**
     * At this point list mutex is held and bb points to a node with
     * enough space available. pos is not yet set.
     */
    pos = &bb->bb_buf[bb->bb_buf_used];
    bb->bb_buf_used += str_len;
    bb->bb_buf_left -= str_len;

    ss_dassert((pos >= &bb->bb_buf[0]) && (pos <= &bb->bb_buf[MAX_LOGSTRLEN-str_len]));

    /** read checkmark */
    /** TODO: add buffer overflow checkmark
        chk_val = (int)bb->bb_buf[bb->bb_buf_used-count_len];
        ss_dassert(chk_val == bb->bb_strcount);
    */

    /** TODO : write next checkmark
        bb->bb_strcount += 1;
        memcpy(&bb->bb_buf[bb->bb_buf_used], &bb->bb_strcount, count_len);
        bb->bb_buf_used += count_len;
        bb->bb_buf_left -= count_len;
    */

    /**
     * If flush flag is set, set buffer full. As a consequence, no-one
     * can write to it before it is flushed to disk.
     */
    bb->bb_state = (flush == true ? BB_FULL : bb->bb_state);

    /** Unlock buffer */
    simple_mutex_unlock(&bb->bb_mutex);
    return pos;
}

static void blockbuf_node_done(void* bb_data)
{
    blockbuf_t* bb = (blockbuf_t *)bb_data;
    simple_mutex_done(&bb->bb_mutex);
}


static blockbuf_t* blockbuf_init()
{
    blockbuf_t* bb;

    if ((bb = (blockbuf_t *) calloc(1, sizeof (blockbuf_t))))
    {
#if defined(SS_DEBUG)
        bb->bb_chk_top = CHK_NUM_BLOCKBUF;
        bb->bb_chk_tail = CHK_NUM_BLOCKBUF;
#endif
        simple_mutex_init(&bb->bb_mutex, "Blockbuf mutex");
        bb->bb_buf_left = MAX_LOGSTRLEN;
        bb->bb_buf_size = MAX_LOGSTRLEN;
#if defined(SS_LOG_DEBUG)
        sprintf(bb->bb_buf, "[block:%d]", atomic_add(&block_start_index, 1));
        bb->bb_buf_used += strlen(bb->bb_buf);
        bb->bb_buf_left -= strlen(bb->bb_buf);
#endif
        CHK_BLOCKBUF(bb);
    }
    else
    {
        fprintf(stderr, "Error: Memory allocation failed when initializing log manager block buffers.");
    }
    return bb;
}

/**
 * Set log augmentation.
 *
 * @param bits One of the log_augmentation_t constants.
 */
void mxs_log_set_augmentation(int bits)
{
    log_config.augmentation = bits & MXS_LOG_AUGMENTATION_MASK;
}

/**
 * Helper for skygw_log_write and friends.
 *
 * @param int        The syslog priority.
 * @param file       The name of the file where the logging was made.
 * @param int        The line where the logging was made.
 * @param function   The function where the logging was made.
 * @param prefix_len The length of the text to be stripped away when syslogging.
 * @param len        Length of str, including terminating NULL.
 * @param str        String
 * @param flush      Whether the message should be flushed.
 *
 * @return 0 if the logging to at least one log succeeded.
 */

static int log_write(int            priority,
                     const char*    file,
                     int            line,
                     const char*    function,
                     size_t         prefix_len,
                     size_t         len,
                     const char*    str,
                     enum log_flush flush)
{
    int rv = -1;

    if (logmanager_register(true))
    {
        CHK_LOGMANAGER(lm);

        rv = logmanager_write_log(priority, flush, prefix_len, len, str);

        logmanager_unregister();
    }

    return rv;
}

/**
 * @node Register as a logging client to logmanager.
 *
 * Parameters:
 * @param lmgr - <usage>
 *          <description>
 *
 * @return
 *
 *
 * @details Link count modify is protected by mutex.
 *
 */
static bool logmanager_register(bool writep)
{
    bool succ = true;

    acquire_lock(&lmlock);

    if (lm == NULL || !lm->lm_enabled)
    {
        /**
         * Flush succeeds if logmanager is shut or shutting down.
         * returning false so that flusher doesn't access logmanager
         * and its members which would probabaly lead to NULL pointer
         * reference.
         */
        if (!writep || fatal_error)
        {
            succ = false;
            goto return_succ;
        }

        ss_dassert(lm == NULL || (lm != NULL && !lm->lm_enabled));

        /**
         * Wait until logmanager shut down has completed.
         * logmanager is enabled if someone already restarted
         * it between latest lock release, and acquire.
         */
        while (lm != NULL && !lm->lm_enabled)
        {
            release_lock(&lmlock);
            pthread_yield();
            acquire_lock(&lmlock);
        }

        if (lm == NULL)
        {
            // If someone is logging before the log manager has been inited,
            // or after the log manager has been finished, the messages are
            // written to stdout.
            succ = logmanager_init_nomutex(NULL, NULL, MXS_LOG_TARGET_DEFAULT);
        }
    }
    /** if logmanager existed or was succesfully restarted, increase link */
    if (succ)
    {
        lm->lm_nlinks += 1;
    }

return_succ:

    if (!succ)
    {
        fatal_error = true;
    }
    release_lock(&lmlock);
    return succ;
}

/**
 * @node Unregister from logmanager.
 *
 * Parameters:
 * @param lmgr - <usage>
 *          <description>
 *
 * @return
 *
 *
 * @details Link count modify is protected by mutex.
 *
 */
static void logmanager_unregister(void)
{
    acquire_lock(&lmlock);

    lm->lm_nlinks -= 1;
    ss_dassert(lm->lm_nlinks >= 0);

    release_lock(&lmlock);
}


/**
 * @node Initialize log file naming parameters from call arguments
 * or from default functions in cases where arguments are not provided.
 *
 * @param fn     The fnames_conf_t structure to initialize.
 * @param logdir The directory for the log file. If NULL logging will be made to stdout.
 *
 * @return True if the initialization was performed, false otherwise.
 *
 * @details Note that input parameter lenghts are checked here.
 *
 */
static bool fnames_conf_init(fnames_conf_t* fn, const char* logdir)
{
    bool succ = false;

    /**
     * When init_started is set, clean must be done for it.
     */
    fn->fn_state = INIT;
#if defined(SS_DEBUG)
    fn->fn_chk_top  = CHK_NUM_FNAMES;
    fn->fn_chk_tail = CHK_NUM_FNAMES;
#endif
    const char* dir;
    if (logdir)
    {
        log_config.use_stdout = false;
        dir = logdir;
    }
    else
    {
        log_config.use_stdout = true;
        // TODO: Re-arrange things so that fn->fn_logpath can be NULL.
        dir = "/tmp";
    }

    fn->fn_logpath = strdup(dir);

    if (fn->fn_logpath)
    {
        succ = true;
        fn->fn_state = RUN;
        CHK_FNAMES_CONF(fn);
    }

    return succ;
}


/**
 * @node Calls logfile initializer for each logfile.
 *
 *
 * Parameters:
 * @param lm            Log manager pointer
 *
 * @return true if succeed, otherwise false.
 *
 *
 * @details If logfile is supposed to be located to shared memory
 * it is specified here. In the case of shared memory file, a soft
 * link is created to log directory.
 *
 * Motivation is performance. LOGFILE_DEBUG, for example, has so much data
 * that writing it to disk slows execution down remarkably.
 *
 */
static bool logfiles_init(logmanager_t* lm)
{
    bool store_shmem = (lm->lm_target == MXS_LOG_TARGET_SHMEM);

    bool succ = logfile_init(&lm->lm_logfile, lm, store_shmem);

    if (!succ)
    {
        fprintf(stderr, "*\n* Error : Initializing log files failed.\n");
    }

    return succ;
}

static void logfile_flush(logfile_t* lf)
{
    CHK_LOGFILE(lf);
    acquire_lock(&lf->lf_spinlock);
    lf->lf_flushflag = true;
    release_lock(&lf->lf_spinlock);
    skygw_message_send(lf->lf_logmes);
}

/**
 * Set rotate flag for a log file and wake up the writer thread which then
 * performs the actual rotation task.
 *
 * @param lf    logfile pointer
 */
static void logfile_rotate(logfile_t* lf)
{
    CHK_LOGFILE(lf);
    acquire_lock(&lf->lf_spinlock);
    lf->lf_rotateflag = true;
    release_lock(&lf->lf_spinlock);
    skygw_message_send(lf->lf_logmes);
}

/**
 * Forms complete path name for logfile and tests that the file doesn't conflict
 * with any existing file and it is writable.
 *
 * @param lf    logfile pointer
 *
 * @return true if succeed, false if failed
 *
 * @note        Log file openings are not TOCTOU-safe. It is not likely that
 * multiple copies of same files are opened in parallel but it is possible by
 * using log manager in parallel with multiple processes and by configuring
 * log manager to use same directories among those processes.
 */
static bool logfile_build_name(logfile_t* lf)
{
    bool namecreatefail;
    bool nameconflicts;
    bool store_shmem;
    bool writable;
    bool succ;
    strpart_t spart[3]; /*< string parts of which the file is composed of */

    if (log_config.use_stdout)
    {
        // TODO: Refactor so that lf_full_file_name can be NULL in this case.
        lf->lf_full_file_name = strdup("stdout");
        succ = true;
        // TODO: Refactor to get rid of the gotos.
        goto return_succ;
    }
    /**
     * sparts is an array but next pointers are used to walk through
     * the list of string parts.
     */
    spart[0].sp_next = &spart[1];
    spart[1].sp_next = &spart[2];
    spart[2].sp_next = NULL;

    spart[1].sp_string = lf->lf_name_prefix;
    spart[2].sp_string = lf->lf_name_suffix;

    store_shmem = lf->lf_store_shmem;

    do
    {
        namecreatefail = false;
        nameconflicts  = false;

        spart[0].sp_string = lf->lf_filepath;
        /**
         * Create name for log file. Seqno is added between prefix &
         * suffix (index == 2)
         */
        lf->lf_full_file_name = form_full_file_name(spart, lf, 2);

        if (store_shmem)
        {
            spart[0].sp_string = lf->lf_linkpath;
            /**
             * Create name for link file
             */
            lf->lf_full_link_name = form_full_file_name(spart, lf, 2);
        }
        /**
         * At least one of the files couldn't be created. Increase
         * sequence number and retry until succeeds.
         */
        if (lf->lf_full_file_name == NULL ||
            (store_shmem && lf->lf_full_link_name == NULL))
        {
            namecreatefail = true;
            goto file_create_fail;
        }

        /**
         * If file exists but is different type, create fails and
         * new, increased sequence number is added to file name.
         */
        if (check_file_and_path(lf->lf_full_file_name, &writable, true))
        {
            /** Found similarly named file which isn't writable */
            if (!writable || file_is_symlink(lf->lf_full_file_name))
            {
                nameconflicts = true;
                goto file_create_fail;
            }
        }
        else
        {
            /**
             * Opening the file failed for some other reason than
             * existing non-writable file. Shut down.
             */
            if (!writable)
            {
                succ = false;
                goto return_succ;
            }
        }

        if (store_shmem)
        {
            if (check_file_and_path(lf->lf_full_link_name, &writable, true))
            {
                /** Found similarly named link which isn't writable */
                if (!writable)
                {
                    nameconflicts = true;
                }
            }
            else
            {
                /**
                 * Opening the file failed for some other reason than
                 * existing non-writable file. Shut down.
                 */
                if (!writable)
                {
                    succ = false;
                    goto return_succ;
                }
            }
        }
    file_create_fail:
        if (namecreatefail || nameconflicts)
        {
            lf->lf_name_seqno += 1;

            if (lf->lf_full_file_name != NULL)
            {
                free(lf->lf_full_file_name);
                lf->lf_full_file_name = NULL;
            }
            if (lf->lf_full_link_name != NULL)
            {
                free(lf->lf_full_link_name);
                lf->lf_full_link_name = NULL;
            }
        }
    }
    while (namecreatefail || nameconflicts);

    succ = true;

return_succ:
    return succ;
}

/**
 * Opens a log file and writes header to the beginning of it. File name, FILE*,
 * and file descriptor are stored to skygw_file_t struct which is stored in
 * filewriter strcuture passed as parameter.
 *
 * @param fw    filewriter pointer
 * @param lf    logfile pointer
 *
 * @return true if succeed; the resulting skygw_file_t is written in filewriter,
 * false if failed.
 *
 */
static bool logfile_open_file(filewriter_t* fw, logfile_t* lf)
{
    bool rv = true;

    if (log_config.use_stdout)
    {
        fw->fwr_file = skygw_file_alloc(lf->lf_full_file_name);
        fw->fwr_file->sf_file = stdout;
    }
    else if (lf->lf_store_shmem)
    {
        /** Create symlink pointing to log file */
        fw->fwr_file = skygw_file_init(lf->lf_full_file_name, lf->lf_full_link_name);
    }
    else
    {
        /** Create normal disk-resident log file */
        fw->fwr_file = skygw_file_init(lf->lf_full_file_name, NULL);
    }

    if (fw->fwr_file == NULL)
    {
        fprintf(stderr, "Error : opening logfile %s failed.\n", lf->lf_full_file_name);
        rv = false;
    }

    return rv;
}


/**
 * @node Combine all name parts from left to right.
 *
 * Parameters:
 * @param parts
 *
 * @param seqno specifies the the sequence number which will be added as a part
 * of full file name. seqno == -1 indicates that sequence number won't be used.
 *
 * @param seqnoidx Specifies the seqno position in the 'array' of name parts.
 * If seqno == -1 seqnoidx will be set -1 as well.
 *
 * @return Pointer to filename, of NULL if failed.
 *
 */
static char* form_full_file_name(strpart_t* parts, logfile_t* lf, int seqnoidx)
{
    int    i;
    int    seqno;
    size_t s;
    size_t fnlen;
    char*  filename = NULL;
    char*  seqnostr = NULL;
    strpart_t* p;

    if (lf->lf_name_seqno != -1)
    {
        int   file_sn;
        int   link_sn = 0;
        const char* tmp = parts[0].sp_string;

        file_sn = find_last_seqno(parts, lf->lf_name_seqno, seqnoidx);

        if (lf->lf_linkpath != NULL)
        {
            tmp = parts[0].sp_string;
            parts[0].sp_string = lf->lf_linkpath;
            link_sn = find_last_seqno(parts, lf->lf_name_seqno, seqnoidx);
            parts[0].sp_string = tmp;
        }
        lf->lf_name_seqno = MAX(file_sn, link_sn);

        seqno = lf->lf_name_seqno;
        s = UINTLEN(seqno);
        seqnostr = (char *)malloc((int)s + 1);
    }
    else
    {
        /**
         * These magic numbers are needed to indicate this and
         * in subroutines that sequence number is not used.
         */
        s = 0;
        seqnoidx = -1;
        seqno = lf->lf_name_seqno;
    }

    if (parts == NULL || parts->sp_string == NULL)
    {
        goto return_filename;
    }
    /**
     * Length of path, file name, separating slash, sequence number and
     * terminating character.
     */
    fnlen = sizeof('/') + s + sizeof('\0');
    p = parts;
    /** Calculate the combined length of all parts put together */
    while (p->sp_string != NULL)
    {
        fnlen += strnlen(p->sp_string, NAME_MAX);

        if (p->sp_next == NULL)
        {
            break;
        }
        p = p->sp_next;
    }

    if (fnlen > NAME_MAX)
    {
        fprintf(stderr, "Error : Too long file name= %d.\n", (int)fnlen);
        goto return_filename;
    }
    filename = (char*)calloc(1, fnlen);

    if (seqnostr != NULL)
    {
        snprintf(seqnostr, s + 1, "%d", seqno);
    }

    for (i = 0, p = parts; p->sp_string != NULL; i++, p = p->sp_next)
    {
        if (seqnostr != NULL && i == seqnoidx)
        {
            strcat(filename, seqnostr); /*< add sequence number */
        }
        strcat(filename, p->sp_string);

        if (p->sp_next == NULL)
        {
            break;
        }
    }

return_filename:
    if (seqnostr != NULL)
    {
        free(seqnostr);
    }

    return filename;
}

/**
 * @node Allocate new buffer where argument string with a slash is copied.
 * Original string buffer is freed.
 * added.
 *
 * Parameters:
 * @param str - <usage>
 *          <description>
 *
 * @return
 *
 *
 * @details (write detailed description here)
 *
 */
static char* add_slash(char* str)
{
    char*  p = str;
    size_t plen = strlen(p);

    /** Add slash if missing */
    if (p[plen - 1] != '/')
    {
        str = (char *)malloc(plen + 2);
        snprintf(str, plen + 2, "%s/", p);
        free(p);
    }
    return str;
}


/**
 * @node Check if the path and file exist in the local file system and if they do,
 * check if they are accessible and writable.
 *
 * Parameters:
 * @param filename      file to be checked
 *
 * @param writable      flag indicating whether file was found writable or not
 * if writable is NULL, check is skipped.
 *
 * @return true & writable if file exists and it is writable,
 *      true & not writable if file exists but it can't be written,
 *      false & writable if file doesn't exist but directory could be written, and
 *      false & not writable if directory can't be written.
 *
 * @details Note, that a space character is written to the end of file.
 * TODO: recall what was the reason for not succeeding with simply
 * calling access, and fstat. vraa 26.11.13
 */
static bool check_file_and_path(char* filename, bool* writable, bool do_log)
{
    bool exists;

    if (filename == NULL)
    {
        exists = false;

        if (writable)
        {
            *writable = false;
        }
    }
    else
    {
        if (access(filename, F_OK) == 0)
        {

            exists = true;

            if (access(filename, W_OK) == 0)
            {
                if (writable)
                {
                    *writable = true;
                }
            }
            else
            {

                if (do_log && file_is_symlink(filename))
                {
                    char errbuf[STRERROR_BUFLEN];
                    fprintf(stderr,
                            "*\n* Error : Can't access "
                            "file pointed to by %s due "
                            "to %s.\n",
                            filename,
                            strerror_r(errno, errbuf, sizeof(errbuf)));
                }
                else if (do_log)
                {
                    char errbuf[STRERROR_BUFLEN];
                    fprintf(stderr,
                            "*\n* Error : Can't access %s due "
                            "to %s.\n",
                            filename,
                            strerror_r(errno, errbuf, sizeof(errbuf)));
                }

                if (writable)
                {
                    *writable = false;
                }
            }

        }
        else
        {
            exists = false;
            if (writable)
            {
                *writable = true;
            }
        }
    }
    return exists;
}



static bool file_is_symlink(char* filename)
{
    int  rc;
    bool succ = false;
    struct stat b;

    if (filename != NULL)
    {
        rc = lstat(filename, &b);

        if (rc != -1 && S_ISLNK(b.st_mode))
        {
            succ = true;
        }
    }
    return succ;
}



/**
 * @node Initialize logfile structure. Form log file name, and optionally
 * link name. Create block buffer for logfile.
 *
 * Parameters:
 * @param logfile       log file
 * @param logmanager    log manager pointer
 * @param store_shmem   flag to indicate whether log is physically written to shmem
 *
 * @return true if succeed, false otherwise
 */
static bool logfile_init(logfile_t*    logfile,
                         logmanager_t* logmanager,
                         bool          store_shmem)
{
    bool           succ = false;
    fnames_conf_t* fn = &logmanager->lm_fnames_conf;
    logfile->lf_state = INIT;
#if defined(SS_DEBUG)
    logfile->lf_chk_top = CHK_NUM_LOGFILE;
    logfile->lf_chk_tail = CHK_NUM_LOGFILE;
#endif
    logfile->lf_logmes = logmanager->lm_logmes;
    logfile->lf_name_prefix = LOGFILE_NAME_PREFIX;
    logfile->lf_name_suffix = LOGFILE_NAME_SUFFIX;
    logfile->lf_npending_writes = 0;
    logfile->lf_name_seqno = 1;
    logfile->lf_lmgr = logmanager;
    logfile->lf_flushflag = false;
    logfile->lf_rotateflag = false;
    logfile->lf_spinlock = 0;
    logfile->lf_store_shmem = store_shmem;
    logfile->lf_buf_size = MAX_LOGSTRLEN;
    /**
     * If file is stored in shared memory in /dev/shm, a link
     * pointing to shm file is created and located to the file
     * directory.
     */
    if (store_shmem)
    {
        char* c;
        pid_t pid = getpid();
        int   len = strlen(shm_pathname_prefix)
            + strlen("maxscale.") +
            get_decimal_len((size_t)pid) + 1;

        c = (char *)calloc(len, sizeof(char));

        if (c == NULL)
        {
            succ = false;
            goto return_with_succ;
        }
        sprintf(c, "%smaxscale.%d", shm_pathname_prefix, pid);
        logfile->lf_filepath = c;

        if (mkdir(c, S_IRWXU | S_IRWXG) != 0 &&
            errno != EEXIST)
        {
            succ = false;
            goto return_with_succ;
        }
        logfile->lf_linkpath = strdup(fn->fn_logpath);
        logfile->lf_linkpath = add_slash(logfile->lf_linkpath);
    }
    else
    {
        logfile->lf_filepath = strdup(fn->fn_logpath);
    }
    logfile->lf_filepath = add_slash(logfile->lf_filepath);

    if (!(succ = logfile_build_name(logfile)))
    {
        goto return_with_succ;
    }
    /**
     * Create a block buffer list for log file. Clients' writes go to buffers
     * from where separate log flusher thread writes them to disk.
     */
    if (mlist_init(&logfile->lf_blockbuf_list,
                   NULL,
                   strdup("logfile block buffer list"),
                   blockbuf_node_done,
                   MAXNBLOCKBUFS) == NULL)
    {
        ss_dfprintf(stderr,
                    "*\n* Error : Initializing buffers for log files "
                    "failed.");
        logfile_free_memory(logfile);
        goto return_with_succ;
    }

    succ = true;
    logfile->lf_state = RUN;
    CHK_LOGFILE(logfile);

return_with_succ:
    if (!succ)
    {
        logfile_done(logfile);
    }
    ss_dassert(logfile->lf_state == RUN || logfile->lf_state == DONE);
    return succ;
}

/**
 * @node Flush logfile and free memory allocated for it.
 *
 * Parameters:
 * @param lf - <usage>
 *          <description>
 *
 * @return void
 *
 *
 * @details Operation is not protected. it is assumed that no one tries
 * to call logfile functions when logfile_done is called.
 *
 * It is also assumed that filewriter doesn't exist anymore.
 *
 * Logfile access could be protected by using exit flag and rwlock to protect
 * flag read/write. Lock would be held during log write operation by clients.
 *
 */
static void logfile_done(logfile_t* lf)
{
    switch (lf->lf_state)
    {
    case RUN:
        CHK_LOGFILE(lf);
        ss_dassert(lf->lf_npending_writes == 0);
        /** fallthrough */
    case INIT:
        /** Test if list is initialized before freeing it */
        if (lf->lf_blockbuf_list.mlist_versno != 0)
        {
            mlist_done(&lf->lf_blockbuf_list);
        }
        logfile_free_memory(lf);
        lf->lf_state = DONE;
        /** fallthrough */
    case DONE:
    case UNINIT:
    default:
        break;
    }
}

static void logfile_free_memory(logfile_t* lf)
{
    if (lf->lf_filepath != NULL)       free(lf->lf_filepath);
    if (lf->lf_linkpath != NULL)       free(lf->lf_linkpath);
    if (lf->lf_full_link_name != NULL) free(lf->lf_full_link_name);
    if (lf->lf_full_file_name != NULL) free(lf->lf_full_file_name);
}

/**
 * @node Initialize filewriter data and open the log file for each log file type.
 *
 * @param logmanager Log manager struct
 * @param fw         File writer struct
 *
 * @return true if succeed, false if failed
 *
 */
static bool filewriter_init(logmanager_t* logmanager, filewriter_t* fw)
{
    bool succ = false;

    CHK_LOGMANAGER(logmanager);
    assert(logmanager->lm_clientmes);
    assert(logmanager->lm_logmes);

    fw->fwr_state = INIT;
#if defined(SS_DEBUG)
    fw->fwr_chk_top = CHK_NUM_FILEWRITER;
    fw->fwr_chk_tail = CHK_NUM_FILEWRITER;
#endif
    fw->fwr_logmgr = logmanager;
    /** Message from filewriter to clients */
    fw->fwr_logmes = logmanager->lm_logmes;
    /** Message from clients to filewriter */
    fw->fwr_clientmes = logmanager->lm_clientmes;

    logfile_t* lf = logmanager_get_logfile(logmanager);

    if (logfile_open_file(fw, lf))
    {
        fw->fwr_state = RUN;
        CHK_FILEWRITER(fw);
        succ = true;
    }
    else
    {
        fprintf(stderr,
                "Error : opening log file %s failed. Exiting "
                "MaxScale\n",
                lf->lf_full_file_name);
        filewriter_done(fw);
    }

    ss_dassert(fw->fwr_state == RUN || fw->fwr_state == DONE);
    return succ;
}

static void filewriter_done(filewriter_t* fw)
{
    switch (fw->fwr_state)
    {
    case RUN:
        CHK_FILEWRITER(fw);
    case INIT:
        fw->fwr_logmes = NULL;
        fw->fwr_clientmes = NULL;
        if (log_config.use_stdout)
        {
            skygw_file_free(fw->fwr_file);
        }
        else
        {
            skygw_file_close(fw->fwr_file, true);
        }
        fw->fwr_state = DONE;
    case DONE:
    case UNINIT:
    default:
        break;
    }
}

static bool thr_flush_file(logmanager_t *lm, filewriter_t *fwr)
{
    /**
     * Get file pointer of current logfile.
     */
    bool do_flushall = thr_flushall_check();
    skygw_file_t *file = fwr->fwr_file;
    logfile_t *lf = &lm->lm_logfile;

    /**
     * read and reset logfile's flush- and rotateflag
     */
    acquire_lock(&lf->lf_spinlock);
    bool flush_logfile  = lf->lf_flushflag;
    bool rotate_logfile = lf->lf_rotateflag;
    lf->lf_flushflag  = false;
    lf->lf_rotateflag = false;
    release_lock(&lf->lf_spinlock);
    /**
     * Log rotation :
     * Close current, and open a new file for the log.
     */
    if (rotate_logfile)
    {
        bool succ;

        lf->lf_name_seqno += 1; /*< new sequence number */

        if (!(succ = logfile_build_name(lf)))
        {
            lf->lf_name_seqno -= 1; /*< restore */
        }
        else if ((succ = logfile_open_file(fwr, lf)))
        {
            if (log_config.use_stdout)
            {
                skygw_file_free(file);
            }
            else
            {
                skygw_file_close(file, false); /*< close old file */
            }
        }

        if (!succ)
        {
            MXS_ERROR("Log rotation failed. "
                      "Creating replacement file %s "
                      "failed. Continuing "
                      "logging to existing file.",
                      lf->lf_full_file_name);
        }
        return true;
    }
    /**
     * get logfile's block buffer list
     */
    mlist_t *bb_list = &lf->lf_blockbuf_list;
#if defined(SS_DEBUG)
    simple_mutex_lock(&bb_list->mlist_mutex, true);
    CHK_MLIST(bb_list);
    simple_mutex_unlock(&bb_list->mlist_mutex);
#endif
    mlist_node_t *node = bb_list->mlist_first;

    while (node != NULL)
    {
        int err = 0;

        CHK_MLIST_NODE(node);
        blockbuf_t *bb = (blockbuf_t *)node->mlnode_data;
        CHK_BLOCKBUF(bb);

        /** Lock block buffer */
        simple_mutex_lock(&bb->bb_mutex, true);

        blockbuf_state_t flush_blockbuf = bb->bb_state;

        if (bb->bb_buf_used != 0 &&
            ((flush_blockbuf == BB_FULL) || flush_logfile || do_flushall))
        {
            /**
             * buffer is at least half-full
             * -> write to disk
             */
            while (bb->bb_refcount > 0)
            {
                simple_mutex_unlock(&bb->bb_mutex);
                simple_mutex_lock(&bb->bb_mutex, true);
            }
            err = skygw_file_write(file,
                                   (void *)bb->bb_buf,
                                   bb->bb_buf_used,
                                   (flush_logfile || do_flushall));
            if (err)
            {
                // TODO: Log this to syslog.
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "Error : Writing to the log-file %s failed due to (%d, %s). "
                        "Disabling writing to the log.",
                        lf->lf_full_file_name,
                        err,
                        strerror_r(err, errbuf, sizeof(errbuf)));

                mxs_log_set_maxlog_enabled(false);
            }
            /**
             * Reset buffer's counters and mark
             * not full.
             */
            bb->bb_buf_left = bb->bb_buf_size;
            bb->bb_buf_used = 0;
            memset(bb->bb_buf, 0, bb->bb_buf_size);
            bb->bb_state = BB_CLEARED;
#if defined(SS_LOG_DEBUG)
            sprintf(bb->bb_buf,"[block:%d]", atomic_add(&block_start_index, 1));
            bb->bb_buf_used += strlen(bb->bb_buf);
            bb->bb_buf_left -= strlen(bb->bb_buf);
#endif

        }
        /** Release lock to block buffer */
        simple_mutex_unlock(&bb->bb_mutex);

        size_t vn1;
        size_t vn2;
        /** Consistent lock-free read on the list */
        do
        {
            while ((vn1 = bb_list->mlist_versno) % 2 != 0)
            {
                continue;
            }
            node = node->mlnode_next;
            vn2 = bb_list->mlist_versno;
        }
        while (vn1 != vn2 && node);

    } /* while (node != NULL) */

    /**
     * Writer's exit flag was set after checking it.
     * Loop is restarted to ensure that all logfiles are
     * flushed.
     */

    bool done = true;

    if (flushall_started_flag)
    {
        flushall_started_flag = false;
        flushall_done_flag = true;
        done = false;
    }

    return done;
}

/**
 * @node Writes block buffers of logfiles to physical log files on disk.
 *
 * Parameters:
 * @param data - thread context, skygw_thread_t
 *          <description>
 *
 * @return
 *
 *
 * @details Waits until receives wake-up message. Scans through block buffer
 * lists of each logfile object.
 *
 * Block buffer is written to log file if
 * 1. bb_state == true,
 * 2. logfile object's lf_flushflag == true, or
 * 3. skygw_thread_must_exit returns true.
 *
 * Log file is flushed (fsync'd) in cases #2 and #3.
 *
 * Concurrency control : block buffer is accessed by file writer (this) and
 * log clients. File writer reads and sets each logfile object's flushflag
 * with spinlock. Another protected section is when block buffer's metadata is
 * read, and optionally the write operation.
 *
 * When block buffer is marked full, logfile's flushflag is set, other
 * log clients don't try to access buffer(s). There may, however, be
 * active writes, on the block in parallel with file writer operations.
 * Each active write corresponds to bb_refcounter values.
 * On the other hand, file writer doesn't process the block buffer before
 * its refcount is zero.
 *
 * Every log file obj. has its own block buffer (linked) list.
 * List is accessed by log clients, which add nodes if necessary, and
 * by file writer which traverses the list and accesses block buffers
 * included in list nodes.
 * List modifications are protected with version numbers.
 * Before
 modification, version is increased by one to be odd. After the
 * completion, it is increased again to even. List can be read only when
 * version is even and read is consistent only if version hasn't changed
 * during the read.
 */
static void* thr_filewriter_fun(void* data)
{
    skygw_thread_t* thr = (skygw_thread_t *)data;
    filewriter_t*   fwr = (filewriter_t *)skygw_thread_get_data(thr);

    flushall_logfiles(false);

    CHK_FILEWRITER(fwr);
    ss_debug(skygw_thread_set_state(thr, THR_RUNNING));

    /** Inform log manager about the state. */
    skygw_message_send(fwr->fwr_clientmes);
    while (!skygw_thread_must_exit(thr))
    {
        /**
         * Wait until new log arrival message appears.
         * Reset message to avoid redundant calls.
         */
        skygw_message_wait(fwr->fwr_logmes);
        if (skygw_thread_must_exit(thr))
        {
            flushall_logfiles(true);
        }

        /** Process all logfiles which have buffered writes. */
        bool done = false;

        while (!done)
        {
            done = thr_flush_file(lm, fwr);

            if (!thr_flushall_check() && skygw_thread_must_exit(thr))
            {
                flushall_logfiles(true);
                done = false;
            }
        }

        if (flushall_done_flag)
        {
            flushall_done_flag = false;
            flushall_logfiles(false);
            skygw_message_send(fwr->fwr_clientmes);
        }

    } /* while (!skygw_thread_must_exit) */

    ss_debug(skygw_thread_set_state(thr, THR_STOPPED));
    /** Inform log manager that file writer thread has stopped. */
    skygw_message_send(fwr->fwr_clientmes);
    return NULL;
}


static void fnames_conf_done(fnames_conf_t* fn)
{
    switch (fn->fn_state)
    {
    case RUN:
        CHK_FNAMES_CONF(fn);
    case INIT:
        fnames_conf_free_memory(fn);
        fn->fn_state = DONE;
    case DONE:
    case UNINIT:
    default:
        break;
    }
}


static void fnames_conf_free_memory(fnames_conf_t* fn)
{
    free(fn->fn_logpath);
}

/**
 * Find the file with biggest sequence number from given directory and return
 * the sequence number.
 *
 * @param parts string parts of which the file name is composed of
 * @param seqno the sequence number to start with, if seqno is -1 just return
 *
 * @return the biggest sequence number used
 */
static int find_last_seqno(strpart_t* parts,
                           int        seqno,
                           int        seqnoidx)
{
    strpart_t* p;
    char*      snstr;
    int        snstrlen;

    if (seqno == -1)
    {
        return seqno;
    }
    snstrlen = UINTLEN(INT_MAX);
    snstr = (char *)calloc(1, snstrlen);
    p = parts;

    while (true)
    {
        int  i;
        char filename[NAME_MAX] = {0};
        /** Form name with next seqno */
        snprintf(snstr, snstrlen, "%d", seqno + 1);

        for (i = 0, p = parts; p->sp_string != NULL; i++, p = p->sp_next)
        {
            if (snstr != NULL && i == seqnoidx)
            {
                strncat(filename, snstr, NAME_MAX - 1); /*< add sequence number */
            }
            strncat(filename, p->sp_string, NAME_MAX - 1);

            if (p->sp_next == NULL)
            {
                break;
            }
        }

        if (check_file_and_path(filename, NULL, false))
        {
            seqno++;
        }
        else
        {
            break;
        }
    }
    free(snstr);

    return seqno;
}

bool thr_flushall_check()
{
    bool rval = false;
    simple_mutex_lock(&lm->lm_mutex, true);
    rval = flushall_flag;
    if (rval && !flushall_started_flag && !flushall_done_flag)
    {
        flushall_started_flag = true;
    }
    simple_mutex_unlock(&lm->lm_mutex);
    return rval;
}

void flushall_logfiles(bool flush)
{
    simple_mutex_lock(&lm->lm_mutex, true);
    flushall_flag = flush;
    simple_mutex_unlock(&lm->lm_mutex);
}

/**
 * Enable/disable syslog logging.
 *
 * @param enabled True, if high precision logging should be enabled, false if it should be disabled.
 */
void mxs_log_set_highprecision_enabled(bool enabled)
{
    log_config.do_highprecision = enabled;

    MXS_NOTICE("highprecision logging is %s.", enabled ? "enabled" : "disabled");
}

/**
 * Enable/disable syslog logging.
 *
 * @param enabled True, if syslog logging should be enabled, false if it should be disabled.
 */
void mxs_log_set_syslog_enabled(bool enabled)
{
    log_config.do_syslog = enabled;

    MXS_NOTICE("syslog logging is %s.", enabled ? "enabled" : "disabled");
}

/**
 * Enable/disable maxscale log logging.
 *
 * @param enabled True, if syslog logging should be enabled, false if it should be disabled.
 */
void mxs_log_set_maxlog_enabled(bool enabled)
{
    log_config.do_maxlog = enabled;

    MXS_NOTICE("maxlog logging is %s.", enabled ? "enabled" : "disabled");
}


/**
 * Explicitly ensure that all pending log messages are flushed.
 *
 * @return 0 if the flushing was successfully initiated, otherwise -1.
 *
 * Note that the return value only indicates whether the flushing was
 * successfully initiated, not whether the actual flushing has been
 * performed.
 */
int mxs_log_flush()
{
    int err = -1;

    if (logmanager_register(false))
    {
        CHK_LOGMANAGER(lm);

        logfile_t *lf = logmanager_get_logfile(lm);
        CHK_LOGFILE(lf);

        logfile_flush(lf);
        err = 0;

        logmanager_unregister();
    }
    else
    {
        ss_dfprintf(stderr, "Can't register to logmanager, flushing failed.\n");
    }

    return err;
}

/**
 * Explicitly ensure that all pending log messages are flushed.
 *
 * @return 0 if the flushing was successfully performed, otherwise -1.
 *
 * When the function returns 0, the flushing has been initiated and the
 * flushing thread has indicated that the operation has been performed.
 * However, 0 will be returned also in the case that the flushing thread
 * for, whatever reason, failed to actually flush the log.
 */
int mxs_log_flush_sync(void)
{
    int err = 0;

    if (!log_config.use_stdout)
    {
        MXS_INFO("Starting log flushing to disk.");
    }

    /** If initialization of the log manager has not been done, lm pointer can be
     * NULL. */
    // TODO: Why is logmanager_register() not called here?
    if (lm)
    {
        flushall_logfiles(true);

        if (skygw_message_send(lm->lm_logmes) == MES_RC_SUCCESS)
        {
            // TODO: Add error handling to skygw_message_wait. Now void.
            skygw_message_wait(lm->lm_clientmes);
        }
        else
        {
            err = -1;
        }
    }

    return err;
}

/**
 * Rotate the log-file. That is, close the current one and create a new one
 * with a larger sequence number.
 *
 * @return 0 if the rotating was successfully initiated, otherwise -1.
 *
 * Note that the return value only indicates whether the rotating was
 * successfully initiated, not whether the actual rotation has been
 * performed.
 */
int mxs_log_rotate()
{
    int err = -1;

    if (logmanager_register(false))
    {
        CHK_LOGMANAGER(lm);

        logfile_t *lf = logmanager_get_logfile(lm);
        CHK_LOGFILE(lf);

        MXS_NOTICE("Log rotation is called for %s.", lf->lf_full_file_name);

        logfile_rotate(lf);
        err = 0;

        logmanager_unregister();
    }
    else
    {
        ss_dfprintf(stderr, "Can't register to logmanager, rotating failed.\n");
    }

    return err;
}

static const char* priority_name(int priority)
{
    switch (priority)
    {
    case LOG_EMERG:
        return "emercency";
    case LOG_ALERT:
        return "alert";
    case LOG_CRIT:
        return "critical";
    case LOG_ERR:
        return "error";
    case LOG_WARNING:
        return "warning";
    case LOG_NOTICE:
        return "notice";
    case LOG_INFO:
        return "informational";
    case LOG_DEBUG:
        return "debug";
    default:
        assert(!true);
        return "unknown";
    }
}

/**
 * Enable/disable a particular syslog priority.
 *
 * @param priority One of the LOG_ERR etc. constants from sys/syslog.h.
 * @param enabled  True if the priority should be enabled, false if it to be disabled.
 *
 * @return 0 if the priority was valid, -1 otherwise.
 */
int mxs_log_set_priority_enabled(int priority, bool enable)
{
    int rv = -1;
    const char* text = (enable ? "enable" : "disable");

    if ((priority & ~LOG_PRIMASK) == 0)
    {
        int bit = (1 << priority);

        if (enable)
        {
            // TODO: Put behind spinlock.
            mxs_log_enabled_priorities |= bit;
        }
        else
        {
            mxs_log_enabled_priorities &= ~bit;
        }

        MXS_NOTICE("The logging of %s messages has been %sd.", priority_name(priority), text);
    }
    else
    {
        MXS_ERROR("Attempt to %s unknown syslog priority %d.", text, priority);
    }

    return rv;
}

typedef struct log_prefix
{
    const char* text; // The prefix, e.g. "error: "
    int len;          // The length of the prefix without the trailing NULL.
} log_prefix_t;

static const char PREFIX_EMERG[]   = "emerg  : ";
static const char PREFIX_ALERT[]   = "alert  : ";
static const char PREFIX_CRIT[]    = "crit   : ";
static const char PREFIX_ERROR[]   = "error  : ";
static const char PREFIX_WARNING[] = "warning: ";
static const char PREFIX_NOTICE[]  = "notice : ";
static const char PREFIX_INFO[]    = "info   : ";
static const char PREFIX_DEBUG[]   = "debug  : ";

static log_prefix_t priority_to_prefix(int priority)
{
    assert((priority & ~LOG_PRIMASK) == 0);

    log_prefix_t prefix;

    switch (priority)
    {
    case LOG_EMERG:
        prefix.text = PREFIX_EMERG;
        prefix.len = sizeof(PREFIX_EMERG);
        break;

    case LOG_ALERT:
        prefix.text = PREFIX_ALERT;
        prefix.len = sizeof(PREFIX_ALERT);
        break;

    case LOG_CRIT:
        prefix.text = PREFIX_CRIT;
        prefix.len = sizeof(PREFIX_CRIT);
        break;

    case LOG_ERR:
        prefix.text = PREFIX_ERROR;
        prefix.len = sizeof(PREFIX_ERROR);
        break;

    case LOG_WARNING:
        prefix.text = PREFIX_WARNING;
        prefix.len = sizeof(PREFIX_WARNING);
        break;

    case LOG_NOTICE:
        prefix.text = PREFIX_NOTICE;
        prefix.len = sizeof(PREFIX_NOTICE);
        break;

    case LOG_INFO:
        prefix.text = PREFIX_INFO;
        prefix.len = sizeof(PREFIX_INFO);
        break;

    case LOG_DEBUG:
        prefix.text = PREFIX_DEBUG;
        prefix.len = sizeof(PREFIX_DEBUG);
        break;

    default:
        assert(!true);
        prefix.text = PREFIX_ERROR;
        prefix.len = sizeof(PREFIX_ERROR);
        break;
    }

    --prefix.len; // Remove trailing NULL.

    return prefix;
}

static enum log_flush priority_to_flush(int priority)
{
    assert((priority & ~LOG_PRIMASK) == 0);

    switch (priority)
    {
    case LOG_EMERG:
    case LOG_ALERT:
    case LOG_CRIT:
    case LOG_ERR:
        return LOG_FLUSH_YES;

    default:
        assert(!true);
    case LOG_WARNING:
    case LOG_NOTICE:
    case LOG_INFO:
    case LOG_DEBUG:
        return LOG_FLUSH_NO;
    }
}

/**
 * Log a message of a particular priority.
 *
 * @param priority One of the syslog constants: LOG_ERR, LOG_WARNING, ...
 * @param file     The name of the file where the message was logged.
 * @param line     The line where the message was logged.
 * @param function The function where the message was logged.
 * @param format   The printf format of the following arguments.
 * @param ...      Optional arguments according to the format.
 */
int mxs_log_message(int priority,
                    const char* file, int line, const char* function,
                    const char* format, ...)
{
    int err = 0;

    assert((priority & ~LOG_PRIMASK) == 0);

    if ((priority & ~LOG_PRIMASK) == 0) // Check that the priority is ok,
    {
        if (MXS_LOG_PRIORITY_IS_ENABLED(priority))
        {
            va_list valist;

            /**
             * Find out the length of log string (to be formatted str).
             */
            va_start(valist, format);
            int message_len = vsnprintf(NULL, 0, format, valist);
            va_end(valist);

            if (message_len >= 0)
            {
                log_prefix_t prefix = priority_to_prefix(priority);

                static const char FORMAT_FUNCTION[] = "(%s): ";

                // Other thread might change log_config.augmentation.
                int augmentation = log_config.augmentation;
                int augmentation_len = 0;

                switch (augmentation)
                {
                case MXS_LOG_AUGMENT_WITH_FUNCTION:
                    augmentation_len = sizeof(FORMAT_FUNCTION) - 1; // Remove trailing 0
                    augmentation_len -= 2; // Remove the %s
                    augmentation_len += strlen(function);
                    break;

                default:
                    break;
                }

                int buffer_len = prefix.len + augmentation_len + message_len + 1; // Trailing NULL

                if (buffer_len > MAX_LOGSTRLEN)
                {
                    message_len -= (buffer_len - MAX_LOGSTRLEN);
                    buffer_len = MAX_LOGSTRLEN;

                    assert(prefix.len + augmentation_len + message_len + 1 == buffer_len);
                }

                char buffer[buffer_len];

                char *prefix_text = buffer;
                char *augmentation_text = buffer + prefix.len;
                char *message_text = buffer + prefix.len + augmentation_len;

                strcpy(prefix_text, prefix.text);

                if (augmentation_len)
                {
                    int len = 0;

                    switch (augmentation)
                    {
                    case MXS_LOG_AUGMENT_WITH_FUNCTION:
                        len = sprintf(augmentation_text, FORMAT_FUNCTION, function);
                        break;

                    default:
                        assert(!true);
                    }

                    assert(len == augmentation_len);
                }

                va_start(valist, format);
                vsnprintf(message_text, message_len + 1, format, valist);
                va_end(valist);

                enum log_flush flush = priority_to_flush(priority);

                err = log_write(priority, file, line, function, prefix.len, buffer_len, buffer, flush);
            }
        }
    }
    else
    {
        MXS_WARNING("Invalid syslog priority: %d", priority);
    }

    return err;
}

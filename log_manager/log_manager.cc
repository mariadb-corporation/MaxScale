/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <skygw_debug.h>
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

#define MAX_PREFIXLEN 250
#define MAX_SUFFIXLEN 250
#define MAX_PATHLEN   512
#define MAXNBLOCKBUFS 10

/**
 * BUFSIZ comes from the system. It equals with block size or
 * its multiplication.
 */
#define MAX_LOGSTRLEN BUFSIZ

#if defined(SS_PROF)
/**
 * These counters may be inaccurate but give some idea of how
 * things are going.
 */

#endif

/**
 * Global log manager pointer and lock variable.
 * lmlock protects logmanager access.
 */
static int lmlock;
static logmanager_t* lm;


/** Writer thread structure */
struct filewriter_st {
        skygw_chk_t        fwr_chk_top;
        flat_obj_state_t   fwr_state;
        logmanager_t*      fwr_logmgr;
        /** Physical files */
        skygw_file_t*      fwr_file[LOGFILE_LAST+1];
        /** fwr_logmes is for messages from log clients */
        skygw_message_t*   fwr_logmes;
        /** fwr_clientmes is for messages to log clients */
        skygw_message_t*   fwr_clientmes;
        skygw_thread_t*    fwr_thread;
        skygw_chk_t        fwr_chk_tail;
};

/**
 * Log client's string is copied to block-sized log buffer, which is passed
 * to file writer thread.
 */
typedef struct blockbuf_st {
        skygw_chk_t    bb_chk_top;
        logfile_id_t   bb_fileid;
        bool           bb_isfull;   /**< closed for disk write */
        simple_mutex_t bb_mutex;    /**< bb_buf_used, bb_isfull */
        int            bb_refcount; /**< protected by list mutex. #of clients */
//        int            bb_blankcount; /**< # of blanks used btw strings */
        size_t         bb_buf_size;
        size_t         bb_buf_left;
        size_t         bb_buf_used;
        char           bb_buf[BUFSIZ];
        skygw_chk_t    bb_chk_tail;
} blockbuf_t;

/**
 * logfile object corresponds to physical file(s) where
 * certain log is written.
 */
struct logfile_st {
        skygw_chk_t      lf_chk_top;
        flat_obj_state_t lf_state;
        bool             lf_init_started;
        bool             lf_enabled;
        logmanager_t*    lf_lmgr;
        /** fwr_logmes is for messages from log clients */
        skygw_message_t* lf_logmes;
        logfile_id_t     lf_id;
        char*            lf_logpath;
        char*            lf_name_prefix;
        char*            lf_name_suffix;
        int              lf_name_sequence;
        char*            lf_full_name;
        int              lf_nfiles_max;
        size_t           lf_file_size;
        /** list of block-sized log buffers */
        mlist_t          lf_blockbuf_list;
        bool             lf_flushflag;
        int              lf_spinlock; /**< lf_flushflag */
        int              lf_npending_writes;
        skygw_chk_t      lf_chk_tail;
};


struct fnames_conf_st {
        skygw_chk_t      fn_chk_top;
        flat_obj_state_t fn_state;
        char*            fn_trace_prefix;
        char*            fn_trace_suffix;
        char*            fn_msg_prefix;
        char*            fn_msg_suffix;
        char*            fn_err_prefix;
        char*            fn_err_suffix;
        char*            fn_logpath;
        skygw_chk_t      fn_chk_tail;
};

struct logmanager_st {
        skygw_chk_t      lm_chk_top;
        bool             lm_enabled;
        int              lm_enabled_logfiles;
        simple_mutex_t   lm_mutex;
        size_t           lm_nlinks;
        /** fwr_logmes is for messages from log clients */
        skygw_message_t* lm_logmes;
        /** fwr_clientmes is for messages to log clients */
        skygw_message_t* lm_clientmes;
        fnames_conf_t    lm_fnames_conf;
        logfile_t        lm_logfile[LOGFILE_LAST+1];
        filewriter_t     lm_filewriter;
        skygw_chk_t      lm_chk_tail;
};


/** Static function declarations */
static bool logfiles_init(logmanager_t* lmgr);
static bool logfile_init(
        logfile_t*     logfile,
        logfile_id_t   logfile_id,
        logmanager_t*  logmanager);
static void logfile_done(logfile_t* logfile);
static void logfile_free_memory(logfile_t* lf);
static void logfile_flush(logfile_t* lf);
static bool filewriter_init(
        logmanager_t*    logmanager,
        filewriter_t*    fw,
        skygw_message_t* clientmes,
        skygw_message_t* logmes);
static void   filewriter_done(filewriter_t* filewriter);
static bool   fnames_conf_init(fnames_conf_t* fn, int argc, char* argv[]);
static void   fnames_conf_done(fnames_conf_t* fn);
static void   fnames_conf_free_memory(fnames_conf_t* fn);
static char*  fname_conf_get_prefix(fnames_conf_t* fn, logfile_id_t id);
static char*  fname_conf_get_suffix(fnames_conf_t* fn, logfile_id_t id);
static void*  thr_filewriter_fun(void* data);
static logfile_t* logmanager_get_logfile(logmanager_t* lm, logfile_id_t id);
static bool logmanager_register(bool writep);
static void logmanager_unregister(void);
static bool logmanager_init_nomutex(int argc, char* argv[]);
static void logmanager_done_nomutex(void);
static int  logmanager_write_log(
        logfile_id_t id,
        bool         flush,
        bool         use_valist,
        bool         spread_down,
        size_t       len,
        char*        str,
        va_list      valist);

static blockbuf_t* blockbuf_init(logfile_id_t id);
static char*       blockbuf_get_writepos(
#if 0
        int**        refcount,
#else
        blockbuf_t** p_bb,
#endif
        logfile_id_t id,
        size_t       str_len,
        bool         flush);

static void blockbuf_register(blockbuf_t* bb);
static void blockbuf_unregister(blockbuf_t* bb);
static bool logfile_set_enabled(logfile_id_t id, bool val);


const char* get_suffix_default(void)
{
        return ".log";        
}

const char* get_trace_prefix_default(void)
{
        return "skygw_trace";
}

const char* get_trace_suffix_default(void)
{
        return get_suffix_default();
}

const char* get_msg_prefix_default(void)
{
        return "skygw_msg";
}

const char* get_msg_suffix_default(void)
{
        return get_suffix_default();
}

const char* get_err_prefix_default(void)
{
        return "skygw_err";
}

const char* get_err_suffix_default(void)
{
        return get_suffix_default();
}

const char* get_logpath_default(void)
{
        return "/tmp";
}

static bool logmanager_init_nomutex(
        int    argc,
        char*  argv[])
{
        fnames_conf_t* fn;
        filewriter_t*  fw;
        int            err;
        bool           succp = false;

        lm = (logmanager_t *)calloc(1, sizeof(logmanager_t));
        lm->lm_chk_top   = CHK_NUM_LOGMANAGER;
        lm->lm_chk_tail  = CHK_NUM_LOGMANAGER;
        lm->lm_clientmes = skygw_message_init();
        lm->lm_logmes    = skygw_message_init();
        lm->lm_enabled_logfiles |= LOGFILE_ERROR;
        lm->lm_enabled_logfiles |= LOGFILE_MESSAGE;
#if defined(SS_DEBUG)
        lm->lm_enabled_logfiles |= LOGFILE_TRACE;
#endif
        fn = &lm->lm_fnames_conf;
        fw = &lm->lm_filewriter;
        fn->fn_state  = UNINIT;
        fw->fwr_state = UNINIT;
        
        /** Initialize configuration including log file naming info */
        if (!fnames_conf_init(fn, argc, argv)) {
            goto return_succp;
        }

        /** Initialize logfiles */
        if(!logfiles_init(lm)) {
            goto return_succp;
        }
        
        /** Initialize filewriter data and open the (first) log file(s)
         * for each log file type. */
        if (!filewriter_init(lm, fw, lm->lm_clientmes, lm->lm_logmes)) {
            goto return_succp;
        }
        
        /** Initialize and start filewriter thread */
        fw->fwr_thread = skygw_thread_init(strdup("filewriter thr"),
                                           thr_filewriter_fun,
                                           (void *)fw);
   
        if ((err = skygw_thread_start(fw->fwr_thread)) != 0) {
            goto return_succp;
        }
        /** Wait message from filewriter_thr */
        skygw_message_wait(fw->fwr_clientmes);

        succp = true;
        lm->lm_enabled = true;
        
return_succp:
        if (err != 0) {
            /** This releases memory of all created objects */
            logmanager_done_nomutex();
            fprintf(stderr, "* Initializing logmanager failed.\n");
        }
        return succp;
}



/** 
 * @node Initializes log managing routines in SkySQL Gateway.
 *
 * Parameters:
 * @param p_ctx - in, give
 *          pointer to memory location where logmanager stores private write
 * buffer.
 *
 * @param argc - in, use
 *          number of arguments in argv array
 *
 * @param argv - in, use
 *          arguments array
 *
 * @return 
 *
 * 
 * @details (write detailed description here)
 *
 */
bool skygw_logmanager_init(
        int    argc,
        char*  argv[])
{
        bool           succp = false;
                
        acquire_lock(&lmlock);

        if (lm != NULL) {
            succp = true;
            goto return_succp;
        }
        
        succp = logmanager_init_nomutex(argc, argv);
        
return_succp:
        release_lock(&lmlock);
        
        return succp;
}


static void logmanager_done_nomutex(void)
{
        int           i;
        logfile_t*    lf;
        filewriter_t* fwr;

        fwr = &lm->lm_filewriter;

        if (fwr->fwr_state == RUN) {
            CHK_FILEWRITER(fwr);
            /** Inform filewriter thread and wait until it has stopped. */
            skygw_thread_set_exitflag(fwr->fwr_thread,
                                      fwr->fwr_logmes,
                                      fwr->fwr_clientmes);
        
            /** Free thread memory */
            skygw_thread_done(fwr->fwr_thread);
        }
        
        /** Free filewriter memory. */
        filewriter_done(fwr);
        
        for (i=LOGFILE_FIRST; i<=LOGFILE_LAST; i++) {
            lf = logmanager_get_logfile(lm, (logfile_id_t)i);            
            /** Release logfile memory */
            logfile_done(lf);
        }
        /** Release messages and finally logmanager memory */
        fnames_conf_done(&lm->lm_fnames_conf);
        skygw_message_done(lm->lm_clientmes);
        skygw_message_done(lm->lm_logmes);

        /** Set global pointer NULL to prevent access to freed data. */
        free(lm);
        lm = NULL;
}


/** 
 * @node This function is provided for atexit() system function. 
 *
 * Parameters:
 * @param void - <usage>
 *          <description>
 *
 * @return void
 *
 * 
 * @details (write detailed description here)
 *
 */
void skygw_logmanager_exit(void)
{
        skygw_logmanager_done();
}

/** 
 * @node End execution of log manager
 *
 * Parameters:
 * @param p_ctx - in, take
 *           pointer to memory location including context pointer. Context will
 *           be freed in this function.
 *
 * @param logmanager - in, use
 *          pointer to logmanager.
 *
 * @return void
 *
 * 
 * @details Stops file writing thread, releases filewriter, and logfiles.
 *
 */
void skygw_logmanager_done(void)
{
        acquire_lock(&lmlock);

        if (lm == NULL) {
            release_lock(&lmlock);
            return;
        }
        CHK_LOGMANAGER(lm);
        /** Mark logmanager unavailable */
        lm->lm_enabled = false;
        
        /** Wait until all users have left or someone shuts down
         * logmanager between lock release and acquire.
         */
        while(lm != NULL && lm->lm_nlinks != 0) {
            release_lock(&lmlock);
            pthread_yield();
            acquire_lock(&lmlock);
        }

        /** Logmanager was already shut down. Return successfully. */
        if (lm == NULL) {
            goto return_void;
        }
        ss_dassert(lm->lm_nlinks == 0);
        logmanager_done_nomutex();

return_void:
        release_lock(&lmlock);
}

static logfile_t* logmanager_get_logfile(
        logmanager_t* lmgr,
        logfile_id_t  id)
{
        logfile_t* lf;
        CHK_LOGMANAGER(lmgr);
        ss_dassert(id >= LOGFILE_FIRST && id <= LOGFILE_LAST);
        lf = &lmgr->lm_logfile[id];

        if (lf->lf_state == RUN) {
            CHK_LOGFILE(lf);
        }
        return lf;
}


/** 
 * @node Finds write position from block buffer for log string and writes there.  
 *
 * Parameters:
 *
 * @param id - in, use
 *          logfile object identifier
 *
 * @param flush - in, use
 *          indicates whether log string must be written to disk immediately
 *
 * @param use_valist - in, use
 *          does write involve formatting of the string and use of valist argument
 *
 * @param spread_down - in, use
 *          if true, log string is spread to all logs having larger id.
 *
 * @param str_len - in, use
 *          length of formatted string
 *
 * @param str - in, use
 *          string to be written to log 
 *
 * @param valist - in, use
 *          variable-length argument list for formatting the string
 *
 * @return 
 *
 * 
 * @details (write detailed description here)
 *
 */
static int logmanager_write_log(
        logfile_id_t  id,
        bool          flush,
        bool          use_valist,
        bool          spread_down,
        size_t        str_len,
        char*         str,
        va_list       valist)
{
        logfile_t*   lf;
        char*        wp;
        int          err = 0;
        blockbuf_t*  bb;
        blockbuf_t*  bb_c;
        int          timestamp_len;
        int          i;

        CHK_LOGMANAGER(lm);
        
        if (id < LOGFILE_FIRST || id > LOGFILE_LAST) {
            char* errstr = "Invalid logfile id argument.";
            /**
             * invalid id, since we don't have logfile yet.
             */
            err = logmanager_write_log(LOGFILE_ERROR,
                                       true,
                                       false,
                                       false,
                                       strlen(errstr)+1,
                                       errstr,
                                       valist);
            if (err != 0) {
                fprintf(stderr,
                        "Writing to logfile %s failed.\n",
                        STRLOGID(LOGFILE_ERROR));
            }
            err = -1;
            ss_dassert(false);            
            goto return_err;
        }
        lf = &lm->lm_logfile[id];
        CHK_LOGFILE(lf);

        /**
         * When string pointer is NULL, case is skygw_log_flush and no
         * writing is involved. With flush && str != NULL case is
         * skygw_log_write_flush.
         */
        if (str == NULL) {
            ss_dassert(flush);
            logfile_flush(lf); /**< here we wake up file writer */ 
        } else {
            timestamp_len = get_timestamp_len();
            /**
             * Seek write position and register to block buffer.
             * Then print formatted string to write position.
             */
            wp = blockbuf_get_writepos(&bb,
                                       id,
                                       timestamp_len-1+str_len,
                                       flush);
            /**
             * Write timestamp with at most <timestamp_len> characters
             * to wp
             */
            timestamp_len = snprint_timestamp(wp, timestamp_len);
            /**
             * Write next string to overwrite terminating null character
             * of the timestamp string.
             */
            if (use_valist) {
                    vsnprintf(wp+timestamp_len-1, str_len, str, valist);
            } else {
                    snprintf(wp+timestamp_len-1, str_len, str);
            }
            /** remove double line feed */
            if (wp[timestamp_len-1+str_len-2] == '\n') {
                    wp[timestamp_len-1+str_len-2]=' ';
            }
            wp[timestamp_len-1+str_len-1]='\n';
            blockbuf_unregister(bb);

            if (spread_down) {
                    /**
                     * Write to target log. If spread_down == true, then write
                     * also to all logs with greater logfile id.
                     * LOGFILE_ERROR   = 1,
                     * LOGFILE_MESSAGE = 2,
                     * LOGFILE_TRACE   = 4
                     *
                     * So everything written to error log will appear in
                     * message and trace log. Messages will be written in
                     * trace log.
                     */
                    for (i=(id<<1); i<=LOGFILE_LAST; i<<=1) {
                            /** pointer to write buffer of larger-id log */
                            char* wp_c;
                            
                            /**< Check if particular log is enabled */
                            if (!(lm->lm_enabled_logfiles & i)) {
                                    continue;
                            }
                            /**
                             * Seek write position and register to block buffer.
                             * Then print formatted string to write position.
                             */
                            wp_c = blockbuf_get_writepos(&bb_c,
                                                         (logfile_id_t)i,
                                                         timestamp_len-1+str_len,
                                                         flush);
                            /**
                             * Copy original string from block buffer to other
                             * logs' block buffers.
                             */
                            snprintf(wp_c, timestamp_len+str_len, wp);
                            
                            /** remove double line feed */
                            if (wp_c[timestamp_len-1+str_len-2] == '\n') {
                                    wp_c[timestamp_len-1+str_len-2]=' ';
                            }
                            wp_c[timestamp_len-1+str_len-1]='\n';
                            
                            /** lock-free unregistration, includes flush if
                             * bb_isfull */
                            blockbuf_unregister(bb_c);
                    }
            } /* if (spread_down) */
        }
        
return_err:
        return err;
}

static void blockbuf_register(
        blockbuf_t* bb)
{
        CHK_BLOCKBUF(bb);
        ss_dassert(bb->bb_refcount >= 0);
        atomic_add(&bb->bb_refcount, 1);
}


static void blockbuf_unregister(
        blockbuf_t* bb)
{
        logfile_t* lf;
        
        CHK_BLOCKBUF(bb);
        ss_dassert(bb->bb_refcount >= 1);
        lf = &lm->lm_logfile[bb->bb_fileid];
        CHK_LOGFILE(lf);
        /**
         * if this is the last client in a full buffer, send write request.
         */
        if (atomic_add(&bb->bb_refcount, -1) == 1 && bb->bb_isfull) {
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
static char* blockbuf_get_writepos(
        blockbuf_t** p_bb,
        logfile_id_t id,
        size_t       str_len,
        bool         flush)
{
        logfile_t*     lf;
        mlist_t*       bb_list;
        char*          pos = NULL;
        mlist_node_t*  node;
        blockbuf_t*    bb;
        ss_debug(bool  succp;)

            
        CHK_LOGMANAGER(lm);
        lf = &lm->lm_logfile[id];
        CHK_LOGFILE(lf);
        bb_list = &lf->lf_blockbuf_list;

        /** Lock list */
        simple_mutex_lock(&bb_list->mlist_mutex, true);
        CHK_MLIST(bb_list);

        if (bb_list->mlist_nodecount > 0) {
            /**
             * At least block buffer exists on the list. 
             */
            node = bb_list->mlist_first;
            
            /** Loop over blockbuf list to find write position */
            while (true) {
                CHK_MLIST_NODE(node);

                /** Unlock list */
                simple_mutex_unlock(&bb_list->mlist_mutex);
                
                bb = (blockbuf_t *)node->mlnode_data;
                CHK_BLOCKBUF(bb);

                /** Lock buffer */
                simple_mutex_lock(&bb->bb_mutex, true);
                
                if (bb->bb_isfull || bb->bb_buf_left < str_len) {
                    /**
                     * This block buffer is too full.
                     * Send flush request to file writer thread. This causes
                     * flushing all buffers, and (eventually) frees buffer space.
                     */
                    blockbuf_register(bb);
                    bb->bb_isfull = true;
                    blockbuf_unregister(bb);

                    /** Unlock buffer */
                    simple_mutex_unlock(&bb->bb_mutex);

                    /** Lock list */
                    simple_mutex_lock(&bb_list->mlist_mutex, true);
                    
                    /**
                     * If next node exists move forward. Else check if there is
                     * space for a new block buffer on the list.
                     */
                    if (node != bb_list->mlist_last) {
                        node = node->mlnode_next;
                        continue;
                    }
                    /**
                     * All buffers on the list are full.
                     */
                    if (bb_list->mlist_nodecount <
                        bb_list->mlist_nodecount_max)
                    {
                        /**
                         * New node is created
                         */
                        bb = blockbuf_init(id);
                        CHK_BLOCKBUF(bb);

                        /**
                         * Increase version to odd to mark list update active
                         * update.
                         */
                        bb_list->mlist_versno += 1;
                        ss_dassert(bb_list->mlist_versno%2 == 1);
                        
                        ss_debug(succp =)
                            mlist_add_data_nomutex(bb_list, bb);
                        ss_dassert(succp);

                        /**
                         * Increase version to even to mark completion of update.
                         */
                        bb_list->mlist_versno += 1;
                        ss_dassert(bb_list->mlist_versno%2 == 0);
                    } else {
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
                } else {
                    /**
                     * There is space for new log string.
                     */
                    break;
                }
            } /** while (true) */
        } else {
            /**
             * Create the first block buffer to logfile's blockbuf list.
             */
            bb = blockbuf_init(id);
            CHK_BLOCKBUF(bb);

            /** Lock buffer */
            simple_mutex_lock(&bb->bb_mutex, true);
            /**
             * Increase version to odd to mark list update active update.
             */
            bb_list->mlist_versno += 1;
            ss_dassert(bb_list->mlist_versno%2 == 1);

            ss_debug(succp =)mlist_add_data_nomutex(bb_list, bb);
            ss_dassert(succp);

            /**
             * Increase version to even to mark completion of update.
             */
            bb_list->mlist_versno += 1;
            ss_dassert(bb_list->mlist_versno%2 == 0);
            
            /** Unlock list */
            simple_mutex_unlock(&bb_list->mlist_mutex);
        } /* if (bb_list->mlist_nodecount > 0) */
        
        ss_dassert(pos == NULL);
        ss_dassert(!(bb->bb_isfull || bb->bb_buf_left < str_len));
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
        
        ss_dassert(pos >= &bb->bb_buf[0] &&
                   pos <= &bb->bb_buf[BUFSIZ-str_len]);
        
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
        bb->bb_isfull = (flush == true ? true : bb->bb_isfull);
        
        /** Unlock buffer */
        simple_mutex_unlock(&bb->bb_mutex);
        return pos;
}
            



static blockbuf_t* blockbuf_init(
        logfile_id_t id)
{
        blockbuf_t* bb;

        bb = (blockbuf_t *)calloc(1, sizeof(blockbuf_t));
        bb->bb_fileid = id;
        bb->bb_chk_top = CHK_NUM_BLOCKBUF;
        bb->bb_chk_tail = CHK_NUM_BLOCKBUF;
        simple_mutex_init(&bb->bb_mutex, "Blockbuf mutex");
        bb->bb_buf_left = BUFSIZ;
        bb->bb_buf_size = BUFSIZ;

        CHK_BLOCKBUF(bb);
        return bb;
}


int skygw_log_enable(
        logfile_id_t id)
{
        bool err = 0;

        if (!logmanager_register(true)) {
            //fprintf(stderr, "ERROR: Can't register to logmanager\n");
            err = -1;
            goto return_err;
        }
        CHK_LOGMANAGER(lm);

        if (logfile_set_enabled(id, true)) {
                lm->lm_enabled_logfiles |= id;
        }
        
        logmanager_unregister();
return_err:
        return err;
}


int skygw_log_disable(
        logfile_id_t id)
{
        bool err = 0;

        if (!logmanager_register(true)) {
            //fprintf(stderr, "ERROR: Can't register to logmanager\n");
            err = -1;
            goto return_err;
        }
        CHK_LOGMANAGER(lm);

        if (logfile_set_enabled(id, false)) {
            lm->lm_enabled_logfiles &= ~id;
        }

        logmanager_unregister();
return_err:
        return err;
}


static bool logfile_set_enabled(
        logfile_id_t id,
        bool         val)
{
        char*        logstr;
        va_list      notused;
        bool         oldval;
        bool         succp = false;
        int          err = 0;
        logfile_t*   lf;
        
        CHK_LOGMANAGER(lm);
        
        if (id < LOGFILE_FIRST || id > LOGFILE_LAST) {
            char* errstr = "Invalid logfile id argument.";
            /**
             * invalid id, since we don't have logfile yet.
             */
            err = logmanager_write_log(LOGFILE_ERROR,
                                       true,
                                       false,
                                       false,
                                       strlen(errstr)+1,
                                       errstr,
                                       notused);
            if (err != 0) {
                fprintf(stderr,
                        "* Writing to logfile %s failed.\n",
                        STRLOGID(LOGFILE_ERROR));
            }
            ss_dassert(false);            
            goto return_succp;
        }
        lf = &lm->lm_logfile[id];
        CHK_LOGFILE(lf);

        if (val) {
            logstr = strdup("---\tLogging is enabled\t--");
        } else {
            logstr = strdup("---\tLogging is disabled\t--");
        }
        oldval = lf->lf_enabled;
        lf->lf_enabled = val;
        err = logmanager_write_log(id,
                                   true,
                                   false,
                                   false,
                                   strlen(logstr)+1,
                                   logstr,
                                   notused);
        free(logstr);
        
        if (err != 0) {
            lf->lf_enabled = oldval;
            fprintf(stderr,
                    "logfile_set_enabled failed. Writing notification to logfile %s "
                    "failed.\n ",
                    STRLOGID(id));
            goto return_succp;
        }
        succp = true;
return_succp:
        return succp;
}


int skygw_log_write_flush(
        logfile_id_t  id,
        char*         str,
        ...)
{
        int     err = 0;
        va_list valist;
        size_t  len;

        if (!logmanager_register(true)) {
            //fprintf(stderr, "ERROR: Can't register to logmanager\n");
            err = -1;
            goto return_err;
        }
        CHK_LOGMANAGER(lm);

        /**
         * If particular log is disabled only unregister and return.
         */
        if (!(lm->lm_enabled_logfiles & id)) {
            err = 1;
            goto return_unregister;
        }
        /**
         * Find out the length of log string (to be formatted str).
         */
        va_start(valist, str);
        len = vsnprintf(NULL, 0, str, valist);
        va_end(valist);
        /**
         * Add one for line feed.
         */
        len += 1;
        /**
         * Write log string to buffer and add to file write list.
         */
        va_start(valist, str);
        err = logmanager_write_log(id, true, true, true, len, str, valist);
        va_end(valist);

        if (err != 0) {
            fprintf(stderr, "skygw_log_write_flush failed.\n");
            goto return_unregister;
        }

return_unregister:
        logmanager_unregister();
return_err:
        return err;
}



int skygw_log_write(
        logfile_id_t  id,
        char*         str,
        ...)
{
        int     err = 0;
        va_list valist;
        size_t  len;
        
        if (!logmanager_register(true)) {
            //fprintf(stderr, "ERROR: Can't register to logmanager\n");
            err = -1;
            goto return_err;
        }
        CHK_LOGMANAGER(lm);

        /**
         * If particular log is disabled only unregister and return.
         */
        if (!(lm->lm_enabled_logfiles & id)) {
                err = 1;
                goto return_unregister;
        }
        /**
         * Find out the length of log string (to be formatted str).
         */
        va_start(valist, str);
        len = vsnprintf(NULL, 0, str, valist);
        va_end(valist);
        /**
         * Add one for line feed.
         */
        len += 1;
        /**
         * Write log string to buffer and add to file write list.
         */
        va_start(valist, str);
        err = logmanager_write_log(id, false, true, true, len, str, valist);
        va_end(valist);

        if (err != 0) {
            fprintf(stderr, "skygw_log_write failed.\n");
            goto return_unregister;
        }

return_unregister:
        logmanager_unregister();
return_err:
        return err;
}


int skygw_log_flush(
        logfile_id_t  id)
{
        int err = 0;
        va_list valist; /**< Dummy, must be present but it is not processed */
        
        if (!logmanager_register(false)) {
            ss_dfprintf(stderr,
                        "Can't register to logmanager, nothing to flush\n");
            goto return_err;
        }
        CHK_LOGMANAGER(lm);
        err = logmanager_write_log(id, true, false, false, 0, NULL, valist);

        if (err != 0) {
            fprintf(stderr, "skygw_log_flush failed.\n");
            goto return_unregister;
        }

return_unregister:
        logmanager_unregister();
return_err:
        return err;
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
static bool logmanager_register(
        bool writep)
{
        bool succp = true;

        acquire_lock(&lmlock);

        if (lm == NULL || !lm->lm_enabled) {
            /**
             * Flush succeeds if logmanager is shut or shutting down.
             * returning false so that flusher doesn't access logmanager
             * and its members which would probabaly lead to NULL pointer
             * reference.
             */
            if (!writep) {
                succp = false;
                goto return_succp;
            }

            ss_dassert(lm == NULL || (lm != NULL && !lm->lm_enabled));

            /**
             * Wait until logmanager shut down has completed.
             * logmanager is enabled if someone already restarted
             * it between latest lock release, and acquire.
             */
            while (lm != NULL && !lm->lm_enabled) {
                release_lock(&lmlock);
                pthread_yield();
                acquire_lock(&lmlock);
            }
            
            if (lm == NULL) {
                succp = logmanager_init_nomutex(0, NULL);
            }
        }
        /** if logmanager existed or was succesfully restarted, increase link */
        if (succp) {
            lm->lm_nlinks += 1;
        }
        
    return_succp:
        release_lock(&lmlock);        
        return succp;
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
 * Parameters:
 * @param fn - <usage>
 *          <description>
 *
 * @param argc - <usage>
 *          <description>
 *
 * @param argv - <usage>
 *          <description>
 *
 * @return pointer to object which is either in RUN or DONE state.
 *
 * 
 * @details 
 *
 */
static bool fnames_conf_init(
        fnames_conf_t* fn,
        int            argc,
        char*          argv[])
{
        int            opt;
        int            i;
        bool           succp = false;
        const char*    argstr =
            "-h - help\n"
            "-a <trace prefix>   ............(\"skygw_trace\")\n"
            "-b <trace suffix>   ............(\".log\")\n"
            "-c <message prefix> ............(\"skygw_msg\")\n"
            "-d <message suffix> ............(\".log\")\n"
            "-e <error prefix>   ............(\"skygw_err\")\n"
            "-f <error suffix>   ............(\".log\")\n"
            "-g <log path>       ............(\"/tmp\")\n";

        /**
         * When init_started is set, clean must be done for it.
         */
        fn->fn_state = INIT;
        fn->fn_chk_top  = CHK_NUM_FNAMES;
        fn->fn_chk_tail = CHK_NUM_FNAMES;
        
        while ((opt = getopt(argc, argv, "+a:b:c:d:e:f:g:h")) != -1) {
            switch (opt) {
                case 'a':
                    fn->fn_trace_prefix = strndup(optarg, MAX_PREFIXLEN);
                    break;

                case 'b':
                    fn->fn_trace_suffix = strndup(optarg, MAX_SUFFIXLEN);
                    break;

                case 'c':
                    fn->fn_msg_prefix = strndup(optarg, MAX_PREFIXLEN);
                    break;

                case 'd':
                    fn->fn_msg_suffix = strndup(optarg, MAX_SUFFIXLEN);
                    break;

                case 'e':
                    fn->fn_err_prefix = strndup(optarg, MAX_PREFIXLEN);
                    break;
                    
                case 'f':
                    fn->fn_err_suffix = strndup(optarg, MAX_SUFFIXLEN);
                    break;

                case 'g':
                    fn->fn_logpath = strndup(optarg, MAX_PATHLEN);
                    break;

                case 'h':
                default:
                    fprintf(stderr,
                            "\nSupported arguments are (default)\n%s\n",
                            argstr);
                    goto return_conf_init;
            } /** switch (opt) */
        }

        fn->fn_trace_prefix = (fn->fn_trace_prefix == NULL) ?
            strdup(get_trace_prefix_default()) : fn->fn_trace_prefix;
        fn->fn_trace_suffix = (fn->fn_trace_suffix == NULL) ?
            strdup(get_trace_suffix_default()) : fn->fn_trace_suffix;
        fn->fn_msg_prefix   = (fn->fn_msg_prefix == NULL) ?
            strdup(get_msg_prefix_default()) : fn->fn_msg_prefix;
        fn->fn_msg_suffix   = (fn->fn_msg_suffix == NULL) ?
            strdup(get_msg_suffix_default()) : fn->fn_msg_suffix;
        fn->fn_err_prefix   = (fn->fn_err_prefix == NULL) ?
            strdup(get_err_prefix_default()) : fn->fn_err_prefix;
        fn->fn_err_suffix   = (fn->fn_err_suffix == NULL) ?
            strdup(get_err_suffix_default()) : fn->fn_err_suffix;
        fn->fn_logpath      = (fn->fn_logpath == NULL) ?
            strdup(get_logpath_default()) : fn->fn_logpath;

        /* ss_dfprintf(stderr, "\n\n\tCommand line : ");
        for (i=0; i<argc; i++) {
            ss_dfprintf(stderr, "%s ", argv[i]);
        }
        ss_dfprintf(stderr, "\n");*/

        fprintf(stderr,
                "Log directory :\t%s\n"
                "Error log     :\t%s1%s\n"
                "Message log   :\t%s1%s\n"
                "Trace log     :\t%s1%s\n\n",
                fn->fn_logpath,
                fn->fn_err_prefix,
                fn->fn_err_suffix,
                fn->fn_msg_prefix,
                fn->fn_msg_suffix,
                fn->fn_trace_prefix,
                fn->fn_trace_suffix);
        
        succp = true;
        fn->fn_state = RUN;
        CHK_FNAMES_CONF(fn);
        
return_conf_init:
        if (!succp) {
            fnames_conf_done(fn);
        }
        ss_dassert(fn->fn_state == RUN || fn->fn_state == DONE);
        return succp;
}


static char* fname_conf_get_prefix(
        fnames_conf_t* fn,
        logfile_id_t   id)
{
        CHK_FNAMES_CONF(fn);
        ss_dassert(id >= LOGFILE_FIRST && id <= LOGFILE_LAST);

        switch (id) {
            case LOGFILE_TRACE:
                return strdup(fn->fn_trace_prefix);
                break;

            case LOGFILE_MESSAGE:
                return strdup(fn->fn_msg_prefix);
                break;

            case LOGFILE_ERROR:
                return strdup(fn->fn_err_prefix);
                break;

            default:
                return NULL;
        }
}

static char* fname_conf_get_suffix(
        fnames_conf_t* fn,
        logfile_id_t   id)
{
        CHK_FNAMES_CONF(fn);
        ss_dassert(id >= LOGFILE_FIRST && id <= LOGFILE_LAST);

        switch (id) {
            case LOGFILE_TRACE:
                return strdup(fn->fn_trace_suffix);
                break;

            case LOGFILE_MESSAGE:
                return strdup(fn->fn_msg_suffix);
                break;

            case LOGFILE_ERROR:
                return strdup(fn->fn_err_suffix);
                break;

            default:
                return NULL;
        }
}


static bool logfiles_init(
        logmanager_t* lm)
{
        bool succp = true;
        int  i     = LOGFILE_FIRST;

        while(i<=LOGFILE_LAST && succp) {
            succp = logfile_init(&lm->lm_logfile[i], (logfile_id_t)i, lm);
         
            if (!succp) {
                fprintf(stderr, "Initializing logfiles failed\n");
                break;
            }
            i <<= 1;
            
        }
        return succp;
}

static void logfile_flush(
        logfile_t* lf)
{
        CHK_LOGFILE(lf);
        acquire_lock(&lf->lf_spinlock);
        lf->lf_flushflag = true;
        release_lock(&lf->lf_spinlock);
        skygw_message_send(lf->lf_logmes);
}

static bool logfile_init(
        logfile_t*     logfile,
        logfile_id_t   logfile_id,
        logmanager_t*  logmanager)
{
        bool           succp = false;
        size_t         namelen;
        size_t         s;
        fnames_conf_t* fn = &logmanager->lm_fnames_conf;

        logfile->lf_state = INIT;
        logfile->lf_chk_top = CHK_NUM_LOGFILE;
        logfile->lf_chk_tail = CHK_NUM_LOGFILE;
        logfile->lf_logmes = logmanager->lm_logmes;
        logfile->lf_id = logfile_id;
        logfile->lf_logpath = strdup(fn->fn_logpath);
        logfile->lf_name_prefix = fname_conf_get_prefix(fn, logfile_id);
        logfile->lf_name_suffix = fname_conf_get_suffix(fn, logfile_id);
        logfile->lf_npending_writes = 0;
        logfile->lf_name_sequence = 1;
        logfile->lf_lmgr = logmanager;
        logfile->lf_flushflag = false;
        logfile->lf_spinlock = 0;
        logfile->lf_enabled = logmanager->lm_enabled_logfiles & logfile_id;
        /** Read existing files to logfile->lf_files_list and create
         * new file for log named after <directory>/<prefix><counter><suffix>
         */
        s = UINTLEN(logfile->lf_name_sequence);
        namelen = strlen(logfile->lf_logpath) +
            sizeof('/') +
            strlen(logfile->lf_name_prefix) +
            s +
            strlen(logfile->lf_name_suffix) +
            sizeof('\0');
        
        logfile->lf_full_name = (char *)malloc(namelen);

        if (logfile->lf_full_name == NULL) {
            fprintf(stderr, "Memory allocation for full logname failed\n");
            goto return_with_succp;
        }
        ss_dassert(logfile->lf_full_name != NULL);
        
        snprintf(logfile->lf_full_name,
                 namelen,
                 "%s/%s%d%s",
                 logfile->lf_logpath,
                 logfile->lf_name_prefix,
                 logfile->lf_name_sequence,
                 logfile->lf_name_suffix);

        if (mlist_init(&logfile->lf_blockbuf_list,
                       NULL,
                       strdup("logfile block buffer list"),
                       NULL,
                       MAXNBLOCKBUFS) == NULL)
        {
            ss_dfprintf(stderr, "Initializing logfile blockbuf list failed\n");
            logfile_free_memory(logfile);
            goto return_with_succp;
        }
        succp = true;
        logfile->lf_state = RUN;
        CHK_LOGFILE(logfile);

return_with_succp:
        if (!succp) {
            logfile_done(logfile);
        }
        ss_dassert(logfile->lf_state == RUN ||
                   logfile->lf_state == DONE);
        return succp;
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
static void logfile_done(
        logfile_t* lf)
{
        switch(lf->lf_state) {
            case RUN:
                CHK_LOGFILE(lf);
                ss_dassert(lf->lf_npending_writes == 0);
            case INIT:
                mlist_done(&lf->lf_blockbuf_list);
                logfile_free_memory(lf);
                lf->lf_state = DONE;
            case DONE:
            case UNINIT:
            default:
                break;
        }
}

static void logfile_free_memory(
        logfile_t* lf)
{
        if (lf->lf_logpath != NULL)     free(lf->lf_logpath);
        if (lf->lf_name_prefix != NULL) free(lf->lf_name_prefix);
        if (lf->lf_name_suffix != NULL) free(lf->lf_name_suffix);
        if (lf->lf_full_name != NULL)   free(lf->lf_full_name);
}

/** 
 * @node Initialize filewriter struct to a given address
 *
 * Parameters:
 * @param fw - <usage>
 *          <description>
 *
 * @return 
 *
 * 
 * @details (write detailed description here)
 *
 */
static bool filewriter_init(
        logmanager_t*    logmanager,
        filewriter_t*    fw,
        skygw_message_t* clientmes,
        skygw_message_t* logmes)
{
        bool         succp = false;
        logfile_t*   lf;
        logfile_id_t id;
        int          i;
        char*        start_msg_str;
        
        CHK_LOGMANAGER(logmanager);

        fw->fwr_state = INIT;
        fw->fwr_chk_top = CHK_NUM_FILEWRITER;
        fw->fwr_chk_tail = CHK_NUM_FILEWRITER;
        fw->fwr_logmgr = logmanager;
        /** Message from filewriter to clients */
        fw->fwr_logmes = logmes;
        /** Message from clients to filewriter */
        fw->fwr_clientmes = clientmes;

        if (fw->fwr_logmes == NULL || fw->fwr_clientmes == NULL) {
            goto return_succp;
        }
        for (i=LOGFILE_FIRST; i<=LOGFILE_LAST; i <<= 1) {
            id = (logfile_id_t)i;
            lf = logmanager_get_logfile(logmanager, id);
            fw->fwr_file[id] = skygw_file_init(lf->lf_full_name);

            if (fw->fwr_file[id] == NULL) {
                goto return_succp;
            }
            if (lf->lf_enabled) {
                start_msg_str = strdup("---\tLogging is enabled.\n");
            } else {
                start_msg_str = strdup("---\tLogging is disabled.\n");
            }
            skygw_file_write(fw->fwr_file[id],
                             (void *)start_msg_str,
                             strlen(start_msg_str),
                             true);
            free(start_msg_str);
        }
        fw->fwr_state = RUN;
        CHK_FILEWRITER(fw);
        succp = true;
return_succp:
        if (!succp) {
            filewriter_done(fw);
        }
        ss_dassert(fw->fwr_state == RUN || fw->fwr_state == DONE);
        return succp;
}

static void filewriter_done(
    filewriter_t* fw)
{
        int           i;
        logfile_id_t  id;

        switch(fw->fwr_state) {
            case RUN:
                CHK_FILEWRITER(fw);
            case INIT:
                fw->fwr_logmes = NULL;
                fw->fwr_clientmes = NULL;            
                for (i=LOGFILE_FIRST; i<=LOGFILE_LAST; i++) {
                    id = (logfile_id_t)i;
                    skygw_file_done(fw->fwr_file[id]);
                }
                fw->fwr_state = DONE;
            case DONE:
            case UNINIT:
            default:
                break;
        }
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
 * 1. bb_isfull == true,
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
 * Before modification, version is increased by one to be odd. After the
 * completion, it is increased again to even. List can be read only when
 * version is even and read is consistent only if version hasn't changed
 * during the read.
 */
static void* thr_filewriter_fun(
        void* data)
{
        skygw_thread_t* thr;
        filewriter_t*   fwr;
        skygw_file_t*   file;
        logfile_t*      lf;
            
        mlist_t*      bb_list;
        blockbuf_t*   bb;
        mlist_node_t* node;
        int           i;
        bool          flush_blockbuf;   /**< flush single block buffer. */
        bool          flush_logfile;    /**< flush logfile */
        bool          flushall_logfiles;/**< flush all logfiles */
        size_t        vn1;
        size_t        vn2;

        thr = (skygw_thread_t *)data;
        fwr = (filewriter_t *)skygw_thread_get_data(thr);
        CHK_FILEWRITER(fwr);
        ss_debug(skygw_thread_set_state(thr, THR_RUNNING));

        /** Inform log manager about the state. */
        skygw_message_send(fwr->fwr_clientmes);

        while(!skygw_thread_must_exit(thr)) {
            /**
             * Wait until new log arrival message appears.
             * Reset message to avoid redundant calls.
             */
            skygw_message_wait(fwr->fwr_logmes);

            flushall_logfiles = skygw_thread_must_exit(thr);
            
            /** Process all logfiles which have buffered writes. */
            for (i=LOGFILE_FIRST; i<=LOGFILE_LAST; i <<= 1) {
            retry_flush_on_exit:
                /**
                 * Get file pointer of current logfile.
                 */
                file = fwr->fwr_file[i];
                lf = &lm->lm_logfile[(logfile_id_t)i];

                /**
                 * read and reset logfile's flushflag
                 */
                acquire_lock(&lf->lf_spinlock);
                flush_logfile = lf->lf_flushflag;
                lf->lf_flushflag = false;
                release_lock(&lf->lf_spinlock);
                
                /**
                 * get logfile's block buffer list
                 */
                bb_list = &lf->lf_blockbuf_list;
#if defined(SS_DEBUG)
                simple_mutex_lock(&bb_list->mlist_mutex, true);
                CHK_MLIST(bb_list);
                simple_mutex_unlock(&bb_list->mlist_mutex);
#endif
                node = bb_list->mlist_first;
                
                while (node != NULL) {
                    CHK_MLIST_NODE(node);
                    bb = (blockbuf_t *)node->mlnode_data;
                    CHK_BLOCKBUF(bb);

                    /** Lock block buffer */
                    simple_mutex_lock(&bb->bb_mutex, true);

                    flush_blockbuf = bb->bb_isfull;
                    
                    if (bb->bb_buf_used != 0 &&
                        (flush_blockbuf || flush_logfile || flushall_logfiles))
                    {
                        /**
                         * buffer is at least half-full -> write to disk
                         */
                        while(bb->bb_refcount > 0) {
                            simple_mutex_unlock(&bb->bb_mutex);
                            simple_mutex_lock(&bb->bb_mutex, true);
                        }

                        skygw_file_write(file,
                                         (void *)bb->bb_buf,
                                         bb->bb_buf_used,
                                         (flush_logfile || flushall_logfiles));
                        /**
                         * Reset buffer's counters and mark not full.
                         */
                        bb->bb_buf_left = bb->bb_buf_size;
                        bb->bb_buf_used = 0;
                        memset(bb->bb_buf, 0, bb->bb_buf_size);
                        bb->bb_isfull = false;
                    }
                    /** Release lock to block buffer */
                    simple_mutex_unlock(&bb->bb_mutex);
                    
                    /** Consistent lock-free read on the list */
                    do {
                        while ((vn1 = bb_list->mlist_versno)%2 != 0);
                        node = node->mlnode_next;
                        vn2 = bb_list->mlist_versno;
                    } while (vn1 != vn2);
                    
                } /* while (node != NULL) */

                /**
                 * Writer's exit flag was set after checking it.
                 * Loop is restarted to ensure that all logfiles are flushed.
                 */
                if (!flushall_logfiles && skygw_thread_must_exit(thr)) {
                        flushall_logfiles = true;
                        i = LOGFILE_FIRST;
                        goto retry_flush_on_exit;
                }
            } /* for */
        } /* while (!skygw_thread_must_exit) */
        
        ss_debug(skygw_thread_set_state(thr, THR_STOPPED));
        /** Inform log manager that file writer thread has stopped. */
        skygw_message_send(fwr->fwr_clientmes);
        return NULL;
}


static void fnames_conf_done(
        fnames_conf_t* fn)
{
        switch (fn->fn_state) {
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


static void fnames_conf_free_memory(
        fnames_conf_t* fn)
{
        if (fn->fn_trace_prefix != NULL) free(fn->fn_trace_prefix);
        if (fn->fn_trace_suffix!= NULL)  free(fn->fn_trace_suffix);
        if (fn->fn_msg_prefix != NULL)   free(fn->fn_msg_prefix);
        if (fn->fn_msg_suffix != NULL)   free(fn->fn_msg_suffix);
        if (fn->fn_err_prefix != NULL)   free(fn->fn_err_prefix);
        if (fn->fn_err_suffix != NULL)   free(fn->fn_err_suffix);
        if (fn->fn_logpath != NULL)      free(fn->fn_logpath);
}

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <skygw_debug.h>
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

#define MAX_PREFIXLEN 250
#define MAX_SUFFIXLEN 250
#define MAX_PATHLEN   512

/** Write buffer structure */
typedef struct logfile_writebuf_st {
        skygw_chk_t    wb_chk_top;
        size_t         wb_bufsize;
        char           wb_buf[1];
} logfile_writebuf_t;

/** Writer thread structure */
struct filewriter_st {
        skygw_chk_t        fwr_chk_top;
        logmanager_t*      fwr_logmgr;
        filewriter_state_t fwr_state;
        /** Physical files */
        skygw_file_t*      fwr_file[LOGFILE_LAST+1];
        /** fwr_logmes is for messages from log clients */
        skygw_message_t*   fwr_logmes;
        /** fwr_clientmes is for messages to log clients */
        skygw_message_t*   fwr_clientmes;
        skygw_thread_t*    fwr_thread;
        skygw_chk_t        fwr_chk_tail;
};

/** logfile object corresponds to physical file(s) where
 * certain log is written.
 */
struct logfile_st {
        skygw_chk_t      lf_chk_top;
        logmanager_t*    lf_lmgr;
        /** fwr_logmes is for messages from log clients */
        skygw_message_t* lf_logmes;
        logfile_state_t  lf_state;
        logfile_id_t     lf_id;
        char*            lf_logpath;
        char*            lf_name_prefix;
        char*            lf_name_suffix;
        int              lf_name_sequence;
        char*            lf_full_name;
        int              lf_nfiles_max;
        size_t           lf_file_size;
        size_t           lf_writebuf_size;
        /** Flat list for write buffers ready for disk writing */
        mlist_t          lf_writebuf_list;
        int              lf_npending_writes;
        skygw_chk_t      lf_chk_tail;
};


struct fnames_conf_st {
        skygw_chk_t fn_chk_top;
        char*       fn_trace_prefix;
        char*       fn_trace_suffix;
        char*       fn_msg_prefix;
        char*       fn_msg_suffix;
        char*       fn_err_prefix;
        char*       fn_err_suffix;
        char*       fn_logpath;
        size_t      fn_bufsize;
        skygw_chk_t fn_chk_tail;
};

struct logmanager_st {
        skygw_chk_t      lm_chk_top;
        bool             lm_enabled;
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
static bool filewriter_init(
        logmanager_t*    logmanager,
        filewriter_t*    fw,
        skygw_message_t* clientmes,
        skygw_message_t* logmes);
static void filewriter_done(filewriter_t* filewriter);
static bool fnames_conf_init(fnames_conf_t* fn, int argc, char* argv[]);
static void fnames_conf_done(fnames_conf_t* fn);
static char* fname_conf_get_prefix(fnames_conf_t* fn, logfile_id_t id);
static char* fname_conf_get_suffix(fnames_conf_t* fn, logfile_id_t id);
static size_t fname_conf_get_bufsize(fnames_conf_t* fn, logfile_id_t id);
static void* thr_filewriter_fun(void* data);
static logfile_writebuf_t** get_or_create_writebuffers(
        void*  ctx,
        size_t len,
        size_t bufsize);
static int logmanager_write(
        void*         ctx,
        logmanager_t* lmgr,
        logfile_id_t  id,
        char*         str,
        bool          flush);
static bool logmanager_register(logmanager_t* lmgr);
static bool logmanager_unregister(logmanager_t* lmgr);
static void logfile_write_buffers(
        logfile_t*           lf,
        logfile_writebuf_t** p_wb,
        char*                str);


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

const size_t get_bufsize_default(void)
{
        return (size_t)256;
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
logmanager_t*  skygw_logmanager_init(
        void** p_ctx,
        int    argc,
        char*  argv[])
{
        logmanager_t*  lmgr = NULL;
        fnames_conf_t* fn;
        filewriter_t*  fw;
        int            err;
                
        ss_dfprintf(stderr, ">> skygw_logmanager_init\n");

        lmgr = (logmanager_t *)calloc(1, sizeof(logmanager_t));
        lmgr->lm_chk_top =   CHK_NUM_LOGMANAGER;
        lmgr->lm_chk_tail =  CHK_NUM_LOGMANAGER;

        if (simple_mutex_init(
                    &lmgr->lm_mutex,
                    strdup("Logmanager mutex")) == NULL)
        {
            goto return_err;
        }
        lmgr->lm_clientmes = skygw_message_init();
        lmgr->lm_logmes =    skygw_message_init();
        fn = &lmgr->lm_fnames_conf;
        fw = &lmgr->lm_filewriter;
        
        /** Initialize configuration including log file naming info */
        if (!fnames_conf_init(fn, argc, argv)) {
            goto return_err;
        }

        /** Initialize logfiles */
        if(!logfiles_init(lmgr)) {
            goto return_err;
        }
        
        /** Initialize filewriter data and open the (first) log file(s)
         * for each log file type. */
        if (!filewriter_init(lmgr, fw, lmgr->lm_clientmes, lmgr->lm_logmes)) {
            goto return_err;
        }
        /** Initialize and start filewriter thread */
        fw->fwr_thread =
            skygw_thread_init(
                    strdup("filewriter thr"),
                    thr_filewriter_fun,
                    (void *)fw);

        if ((err = skygw_thread_start(fw->fwr_thread)) != 0) {
            goto return_err;
        }
        /** Wait message from filewriter_thr */
        skygw_message_wait(fw->fwr_clientmes);
        
return_mgr:
        lmgr->lm_enabled = TRUE;
        ss_dfprintf(stderr, "<< skygw_logmanager_init\n");
        return lmgr;
        
return_err:
        /** This includes clear-up for all created objects */
        skygw_logmanager_done(NULL, &lmgr);
        fprintf(stderr, "Initializing logmanager failed.\n");
        goto return_mgr;
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
void skygw_logmanager_done(
        void**         p_ctx,
        logmanager_t** logmanager)
{
        int           i;
        filewriter_t* fwr;
        logfile_t*    lf;
        logmanager_t* lmgr;

        ss_dfprintf(stderr, ">> skygw_logmanager_done\n");
        lmgr = *logmanager;
        
        if (lmgr == NULL) {
            fprintf(stderr, "Error. Logmanager is not initialized.\n");
            return;
        }
        CHK_LOGMANAGER(lmgr);
        lmgr->lm_enabled = FALSE;

        while (TRUE) {
            simple_mutex_lock(&lmgr->lm_mutex, TRUE);

            if (lmgr->lm_nlinks == 0) {
                simple_mutex_unlock(&lmgr->lm_mutex);
                break;
            }
            simple_mutex_unlock(&lmgr->lm_mutex);
            usleep(100*MSEC_USEC);
        }
        
        fwr = &lmgr->lm_filewriter;
        CHK_FILEWRITER(fwr);
        
        /** Inform filewriter thread and wait until it has stopped. */
        skygw_thread_set_exitflag(fwr->fwr_thread,
                                  fwr->fwr_logmes,
                                  fwr->fwr_clientmes);
        /** Free thread memory */
        skygw_thread_done(fwr->fwr_thread);
        /** Free filewriter memory. */
        filewriter_done(fwr);
        
        for (i=LOGFILE_FIRST; i<=LOGFILE_LAST; i++) {
            lf = &lmgr->lm_logfile[i];
            CHK_LOGFILE(lf);
            
            /** Release logfile memory */
            logfile_done(lf);
        }
        
        fnames_conf_done(&lmgr->lm_fnames_conf);
        skygw_message_done(lmgr->lm_clientmes);
        skygw_message_done(lmgr->lm_logmes);
        simple_mutex_done(&lmgr->lm_mutex);
        free(lmgr);
        *logmanager = NULL;
        
        ss_dfprintf(stderr, "<< skygw_logmanager_done\n");
}

static logfile_t* logmanager_get_logfile(
        logmanager_t* lmgr,
        logfile_id_t  id)
{
        CHK_LOGMANAGER(lmgr);
        ss_dassert(id >= LOGFILE_FIRST && id <= LOGFILE_LAST);
        CHK_LOGFILE(lmgr->lm_logfile);

        return &lmgr->lm_logfile[id];
}

static int logmanager_write(
        void*         ctx,
        logmanager_t* lmgr,
        logfile_id_t  id,
        char*         str,
        bool          flush)
{
        logfile_t*           lf;
        /** array of constan-size buffers */
        logfile_writebuf_t** wb_arr;
        int                  err = 0;

        CHK_LOGMANAGER(lmgr);
        ss_dassert(id >= LOGFILE_FIRST && id <= LOGFILE_LAST);
        
        if (id < LOGFILE_FIRST || id > LOGFILE_LAST) {
            /** invalid id, since we don't have logfile yet,
             * recall logmanager_write. */
            err = logmanager_write(NULL,
                                   lmgr,
                                   LOGFILE_ERROR,
                                   strdup("Invalid logfile id argument."),
                                   TRUE);
            if (err != 0) {
                fprintf(stderr,
                        "Writing to logfile %s failed.\n",
                        STRLOGID(LOGFILE_ERROR));
            }
            err = -1;
            goto return_err;
        }
        /** Get logfile and check its correctness. */
        lf = logmanager_get_logfile(lmgr, id);
        CHK_LOGFILE(lf);

        if (lf == NULL) {
            fprintf(stderr, "Could find valid logfile from the log manager.\n");
            err = -1;
            goto return_err;
        }
        /** skygw_log_flush doesn't pass any string */
        if (str != NULL) {
            /** Get or create array of constant-size buffers. */
            wb_arr = get_or_create_writebuffers(
                    ctx,
                    strlen(str),
                    lf->lf_writebuf_size);
            
            if (wb_arr == NULL) {
                fprintf(stderr, "Getting or creating write buffers failed.\n");
                err = -1;
                goto return_err;
            }
            /** Split loginfo to buffers, if necessary, and add buffers
             * to logfile.
             * Free write buffer pointer array, and original string. */
            logfile_write_buffers(lf, wb_arr, str);
        } else {
            ss_dassert(flush);
        }
        /** Send notification to filewriter thread */
        if (flush) {
            skygw_message_send(lf->lf_logmes);
        }
        
return_err:
        return err;
}


/** 
 * @node Get or allocate new buffers for log writing.  
 *
 * Parameters:
 * @param ctx - <usage>
 *          <description>
 *
 * @param len - <usage>
 *          <description>
 *
 * @param bufsize - <usage>
 *          <description>
 *
 * @return 
 *
 * 
 * @details Pointers to created buffers are stored to pointer array p_str.
 *
 */
static logfile_writebuf_t** get_or_create_writebuffers(
        void*  ctx,
        size_t len,
        size_t bufsize)
{
        int                  i;
        logfile_writebuf_t** p_wb;
        size_t               allocsize;
        int                  nbufs;
        size_t               llen;
        /** Additional pointer is left to NULL to mark the end of array. */

        /** Allocate pointer array for buffer pointers. */
        llen = len+strlen("\n");
        nbufs = llen/bufsize;
        nbufs = (llen%bufsize)>0 ? nbufs+1 : nbufs;
        p_wb = (logfile_writebuf_t **)calloc(nbufs+1, sizeof(char *));

        if (p_wb == NULL) {
            fprintf(stderr,
                    "Allocating memory for write buffer "
                    "pointer array failed.\n"); 
            goto return_p_str;
        }
        /** Allocate memory for all write buffers.
         * Real allocation size includes logfile_writebuf_t and bufsize.
         * -1 is the one byte defined in logfile_writebuf_st.
         */
        allocsize = sizeof(logfile_writebuf_t)+bufsize-1;
        *p_wb = (logfile_writebuf_t *)calloc(nbufs, allocsize);

        if (*p_wb == NULL) {
            fprintf(stderr,
                    "Allocating memory for write buffer "
                    "pointer array failed.\n");
            free(*p_wb);
            free(p_wb);
            *p_wb = NULL;
            goto return_p_str;
        }

        /** Store pointers of each buffer from continuous memory chunk
         * to p_str. Initialize each write buffer.
         */
        for (i=0; i<nbufs; i++) {
            p_wb[i] = (logfile_writebuf_t *)((char*)*p_wb)+i*allocsize;
            p_wb[i]->wb_chk_top = CHK_NUM_WRITEBUF;
            p_wb[i]->wb_bufsize = bufsize;
        }
        ss_dassert(p_wb[i] == NULL);

return_p_str:
        return p_wb;
}

static void logfile_write_buffers(
        logfile_t*           lf,
        logfile_writebuf_t** p_wb,
        char*                str)
{
        mlist_t*             wblist;
        logfile_writebuf_t** p_data;
        logfile_writebuf_t*  wb;
        char*                p;
        size_t               slen;
        size_t               copylen;
        
        CHK_LOGFILE(lf);
        slen = strlen(str);
        p_data = p_wb;
        p = str;

        /** Copy log string to write buffer(s) */
        while(slen > 0) {
            wb = *p_wb;
            copylen = MIN(wb->wb_bufsize, slen);
            ss_dassert(*p_data != NULL);
            memcpy(wb->wb_buf, p, copylen);
            /** force adding terminating '\0' */
            memset(&wb->wb_buf[copylen], 0, 1);
            /** If this is last buffer, add line feed */
            if (copylen < wb->wb_bufsize) {
                strcpy(&wb->wb_buf[copylen], "\n");
            }
            p_wb += 1;
            p += copylen;
            slen -= copylen;
        }
        /** Release log string */
        free(str);
        ss_dassert(slen == 0);
        ss_dassert(*p_wb == NULL);
        p_wb = p_data;
        
        wblist = &lf->lf_writebuf_list;
        simple_mutex_lock(&wblist->mlist_mutex, TRUE);

        /** Add write buffers to write buffers list from where
         * filewriter reads them. */
        while(*p_wb != NULL) {
            mlist_add_data_nomutex(wblist, *p_wb);
            p_wb += 1;
        }
        simple_mutex_unlock(&wblist->mlist_mutex);
        
        ss_dassert(*p_wb == NULL);
        /** Free pointer array memory */
        free(p_data);
        
}

int skygw_log_write_flush(
        void*         ctx,
        logmanager_t* lmgr,
        logfile_id_t  id,
        char*         str)
{
        int err = 0;
        
        if (lmgr == NULL) {
            fprintf(stderr, "Error. Logmanager is not initialized.\n");
            err = -1;
            goto return_err;
        }
        CHK_LOGMANAGER(lmgr);
        
        if (!logmanager_register(lmgr)) {
            fprintf(stderr, "Logmanager is not available\n");
            err = -1;
            goto return_err;
        }
        ss_dfprintf(stderr,
                    "skygw_log_write_flush writes to %s :\n\t%s.\n",
                    STRLOGID(id),
                    str);
        err = logmanager_write(ctx, lmgr, id, str, TRUE);

        if (err != 0) {
            fprintf(stderr, "skygw_log_write_flush failed.\n");
            goto return_unregister;
        }
        ss_dfprintf(stderr, "skygw_log_write_flush succeeed.\n");

return_unregister:
        logmanager_unregister(lmgr);
return_err:
        return err;
}

int skygw_log_write(
        void*         ctx,
        logmanager_t* lmgr,
        logfile_id_t  id,
        char*         str)
{
        int err = 0;

        if (lmgr == NULL) {
            fprintf(stderr, "Error. Logmanager is not initialized.\n");
            err = -1;
            goto return_err;
        }
        CHK_LOGMANAGER(lmgr);
        
        if (!logmanager_register(lmgr)) {
            fprintf(stderr, "Logmanager is not available\n");
            err = -1;
            goto return_err;
        }
        ss_dfprintf(stderr,
                    "skygw_log_write writes to %s :\n\t%s.\n",
                    STRLOGID(id),
                    str);
        err = logmanager_write(ctx, lmgr, id, str, FALSE);

        if (err != 0) {
            fprintf(stderr, "skygw_log_write failed.\n");
            goto return_unregister;
        }

        ss_dfprintf(stderr, "skygw_log_write succeeed.\n");

return_unregister:
        logmanager_unregister(lmgr);
return_err:
        return err;
}

int skygw_log_flush(
        logmanager_t* lmgr,
        logfile_id_t  id)
{
        int err = 0;
        CHK_LOGMANAGER(lmgr);
        
        if (!logmanager_register(lmgr)) {
            fprintf(stderr, "Logmanager is not available\n");
            err = -1;
            goto return_err;
        }
        err = logmanager_write(NULL, lmgr, id, NULL, TRUE);

        if (err != 0) {
            fprintf(stderr, "skygw_log_flush failed.\n");
            goto return_unregister;
        }
        ss_dfprintf(stderr,
                    "skygw_log_flush : flushed %s successfully.\n",
                    STRLOGID(id));
return_unregister:
        logmanager_unregister(lmgr);
return_err:
        return err;
}

/** 
 * @node Increase link count of logmanager. 
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
        logmanager_t* lmgr)
{
        bool succp = FALSE;
        int  err;
        
        err = simple_mutex_lock(&lmgr->lm_mutex, TRUE);

        if (err != 0) {
            goto return_succp;
        }
        succp = lmgr->lm_enabled;
        
        if (succp) {
            lmgr->lm_nlinks += 1;
        }
        simple_mutex_unlock(&lmgr->lm_mutex);
return_succp:
        return succp;
}

/** 
 * @node Decrease link count of logmanager. 
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
static bool logmanager_unregister(
        logmanager_t* lmgr)
{
        bool succp = FALSE;
        int  err;
        
        err = simple_mutex_lock(&lmgr->lm_mutex, TRUE);

        if (err != 0) {
            goto return_succp;
        }
        succp = lmgr->lm_enabled;
        ss_dassert(succp);
        
        if (succp) {
            lmgr->lm_nlinks -= 1;
            ss_dassert(lmgr->lm_nlinks >= 0);
        }
        simple_mutex_unlock(&lmgr->lm_mutex);
return_succp:
        return succp;
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
 * @return 
 *
 * 
 * @details (write detailed description here)
 *
 */
static bool fnames_conf_init(
        fnames_conf_t* fn,
        int            argc,
        char*          argv[])
{
        int            opt;
        int            i;
        bool           succp = FALSE;
        const char*    argstr =
            "* a - trace prefix,   default \"skygw_trace\"\n"
            "* b - trace suffix,   default \".log\"\n"
            "* c - message prefix, default \"skygw_msg\"\n"
            "* d - message suffix, default \".log\"\n"
            "* e - error prefix,   default \"skygw_err\"\n"
            "* f - error suffix,   default \".log\"\n"
            "* g - log path,       default \"/tmp\"\n"
            "* h - write buffer size in bytes, default 256\n";

        fn->fn_chk_top  = CHK_NUM_FNAMES;
        fn->fn_chk_tail = CHK_NUM_FNAMES;
        
        while ((opt = getopt(argc, argv, "+a:b:c:d:e:f:g:h:")) != -1) {
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
                    fn->fn_bufsize = strtoul(optarg, NULL, 10);
                    break;
                    
                default:
                    fprintf(stderr,
                            "\nSupported arguments are\n%s\n",
                            argstr);
                    goto return_conf_init;
            } /** switch (opt) */
        }

        fn->fn_trace_prefix = (fn->fn_trace_prefix == NULL) ?
            strdup(get_trace_prefix_default()) : NULL;
        fn->fn_trace_suffix = (fn->fn_trace_suffix == NULL) ?
            strdup(get_trace_suffix_default()) : NULL;
        fn->fn_msg_prefix   = (fn->fn_msg_prefix == NULL) ?
            strdup(get_msg_prefix_default()) : NULL;
        fn->fn_msg_suffix   = (fn->fn_msg_suffix == NULL) ?
            strdup(get_msg_suffix_default()) : NULL;
        fn->fn_err_prefix   = (fn->fn_err_prefix == NULL) ?
            strdup(get_err_prefix_default()) : NULL;
        fn->fn_err_suffix   = (fn->fn_err_suffix == NULL) ?
            strdup(get_err_suffix_default()) : NULL;
        fn->fn_logpath      = (fn->fn_logpath == NULL) ?
            strdup(get_logpath_default()) : NULL;
        fn->fn_bufsize      = (fn->fn_bufsize == 0) ?
            get_bufsize_default() : 0;

        ss_dfprintf(stderr, "Command line : ");
        for (i=0; i<argc; i++) {
            ss_dfprintf(stderr, "%s ", argv[i]);
        }
        ss_dfprintf(stderr, "\n");

        succp = TRUE;
return_conf_init:
        CHK_FNAMES_CONF(fn);
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
                return fn->fn_trace_prefix;
                break;

            case LOGFILE_MESSAGE:
                return fn->fn_msg_prefix;
                break;

            case LOGFILE_ERROR:
                return fn->fn_err_prefix;
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
                return fn->fn_trace_suffix;
                break;

            case LOGFILE_MESSAGE:
                return fn->fn_msg_suffix;
                break;

            case LOGFILE_ERROR:
                return fn->fn_err_suffix;
                break;

            default:
                return NULL;
        }
}

static size_t fname_conf_get_bufsize(
        fnames_conf_t* fn,
        logfile_id_t   id)
{
        CHK_FNAMES_CONF(fn);
        ss_dassert(id >= LOGFILE_FIRST && id <= LOGFILE_LAST);

        switch (id) {
            case LOGFILE_TRACE:
            case LOGFILE_MESSAGE:
            case LOGFILE_ERROR:
                return fn->fn_bufsize;
                break;

            default:
                return 0;
        }
}

static bool logfiles_init(
        logmanager_t* lmgr)
{
        bool succp = TRUE;
        int  i     = LOGFILE_FIRST;

        while(i<=LOGFILE_LAST && succp) {
            succp = logfile_init(&lmgr->lm_logfile[i], (logfile_id_t)i, lmgr);

            if (!succp) {
                fprintf(stderr, "Initializing logfiles failed\n");
                break;
            }
            i++;
        }
        return succp;
}


static bool logfile_init(
        logfile_t*     logfile,
        logfile_id_t   logfile_id,
        logmanager_t*  logmanager)
{
        bool           succp = FALSE;
        size_t         namelen;
        fnames_conf_t* fn = &logmanager->lm_fnames_conf;
        
        logfile->lf_chk_top = CHK_NUM_LOGFILE;
        logfile->lf_chk_tail = CHK_NUM_LOGFILE;
        logfile->lf_logmes = logmanager->lm_logmes;
        logfile->lf_state = LOGFILE_INIT;
        logfile->lf_id = logfile_id;
        logfile->lf_logpath = strdup(fn->fn_logpath);
        logfile->lf_name_prefix = fname_conf_get_prefix(fn, logfile_id);
        logfile->lf_name_suffix = fname_conf_get_suffix(fn, logfile_id);
        logfile->lf_npending_writes = 0;
        logfile->lf_name_sequence = 1;
        logfile->lf_lmgr = logmanager;
        /** Read existing files to logfile->lf_files_list and create
         * new file for log named after <directory>/<prefix><counter><suffix>
         */
        namelen = strlen(logfile->lf_logpath) +
            sizeof('/') +
            strlen(logfile->lf_name_prefix) +
            printf("%d",logfile->lf_name_sequence) +
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

        logfile->lf_writebuf_size = fname_conf_get_bufsize(fn, logfile_id);

        if (mlist_init(&logfile->lf_writebuf_list,
                       NULL,
                       strdup("logfile writebuf list")) == NULL)
        {
            ss_dfprintf(stderr, "Initializing logfile writebuf list failed\n");
            logfile_free_memory(logfile);
            goto return_with_succp;
        }
        CHK_LOGFILE(logfile);
        succp = TRUE;
        
return_with_succp:
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
        CHK_LOGFILE(lf);
        ss_dassert(lf->lf_lmgr->lm_filewriter.fwr_state == FILEWRITER_DONE);
        
        if (lf->lf_npending_writes != 0) {
            /** Not implemented yet */
            //logfile_write_flush(lf);
        }
        mlist_done(&lf->lf_writebuf_list);
        logfile_free_memory(lf);
}

static void logfile_free_memory(
        logfile_t* lf)
{
        CHK_LOGFILE(lf);
        free(lf->lf_logpath);
        free(lf->lf_name_prefix);
        free(lf->lf_name_suffix);
        free(lf->lf_full_name);
        lf->lf_state = LOGFILE_DONE;
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
        bool         succp = FALSE;
        logfile_t*   lf;
        logfile_id_t id;
        int          i;
        CHK_LOGMANAGER(logmanager);
        
        fw->fwr_chk_top = CHK_NUM_FILEWRITER;
        fw->fwr_chk_tail = CHK_NUM_FILEWRITER;
        fw->fwr_logmgr = logmanager;
        fw->fwr_state = FILEWRITER_INIT;
        /** Message from filewriter to clients */
        fw->fwr_logmes = logmes;
        /** Message from clients to filewriter */
        fw->fwr_clientmes = clientmes;
        
        if (fw->fwr_logmes == NULL || fw->fwr_clientmes == NULL) {
            goto return_succp;
        }
        for (i=LOGFILE_FIRST; i<=LOGFILE_LAST; i++) {
            id = (logfile_id_t)i;
            lf = logmanager_get_logfile(logmanager, id);
            fw->fwr_file[id] = skygw_file_init(lf->lf_full_name);
        }
        fw->fwr_state = FILEWRITER_RUN;
        succp = TRUE;
return_succp:
        return succp;
}


static void filewriter_done(
    filewriter_t* fw)
{
        int           i;
        logfile_id_t  id;
        
        CHK_FILEWRITER(fw);
        
        fw->fwr_logmes = NULL;
        fw->fwr_clientmes = NULL;
        fw->fwr_state = FILEWRITER_DONE;

        for (i=LOGFILE_FIRST; i<=LOGFILE_LAST; i++) {
            id = (logfile_id_t)i;
            skygw_file_done(fw->fwr_file[id]);
        }
}


/** 
 * @node File writer thread routine 
 *
 * Parameters:
 * @param data - <usage>
 *          <description>
 *
 * @return 
 *
 * 
 * @details (write detailed description here)
 *
 */
static void* thr_filewriter_fun(
        void* data)
{
        skygw_thread_t* thr;
        filewriter_t*   fwr;
        logfile_t*      lf;
        skygw_file_t*   file;
            
        logfile_writebuf_t* wb;
        char*               writep;
        size_t              nbytes;
        logfile_id_t        id;
        mlist_t*            wblist;
        mlist_node_t*       node;
        mlist_node_t*       anchor;
        int                 i;
        bool                succp;

        thr = (skygw_thread_t *)data;
        fwr = (filewriter_t *)skygw_thread_get_data(thr);
        CHK_FILEWRITER(fwr);
        skygw_thread_set_state(thr, THR_RUNNING);
        skygw_message_send(fwr->fwr_clientmes);

        while(!skygw_thread_must_exit(thr)) {
            /** Wait until new log arrival message appears.
             * Reset message to avoid redundant calls. */
            skygw_message_wait(fwr->fwr_logmes);
            skygw_message_reset(fwr->fwr_logmes);

            /** Test _all_ logfiles if they have buffered writes. */
            for (i=LOGFILE_FIRST; i<=LOGFILE_LAST; i++) {
                id = (logfile_id_t)i;
                lf = logmanager_get_logfile(fwr->fwr_logmgr, id);
                CHK_LOGFILE(lf);
                file = fwr->fwr_file[id];
                wblist = &lf->lf_writebuf_list;
                
                simple_mutex_lock(&wblist->mlist_mutex, TRUE);

                /** Process non-empty write buffer lists. */
                if (wblist->mlist_nodecount != 0) {
                    /** Detach nodes from the list and release lock */
                    node = wblist->mlist_first;
                    wblist->mlist_first = NULL;
                    wblist->mlist_last = NULL;
                    wblist->mlist_nodecount = 0;
                    simple_mutex_unlock(&wblist->mlist_mutex);
                    
                    /** Read data from write buf list and write it to file. */
                    while(node != NULL) {
                        wb = (logfile_writebuf_t*)node->mlnode_data;
                        writep = wb->wb_buf;
                        nbytes = strlen(writep);
                        succp = skygw_file_write(file, (void *)writep, nbytes);
                        ss_dassert(succp);
                        anchor = node;
                        node = node->mlnode_next;
                        mlist_node_done(anchor);
                    }
                } else {
                    simple_mutex_unlock(&wblist->mlist_mutex);
                }
            } /* for */
        } /* while */
        /** This informs client thread that file writer thread is stopped. */
        skygw_thread_set_state(thr, THR_STOPPED);
        skygw_message_send(fwr->fwr_clientmes);
        return NULL;
}

static void fnames_conf_done(
        fnames_conf_t* fn)
{
        free(fn->fn_logpath);
        memset(fn, 0, sizeof(fnames_conf_t));
}

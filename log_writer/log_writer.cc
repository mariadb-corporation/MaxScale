#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <skygw_debug.h>
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_writer.h>

#define log_fname_prefix "skygw_log"
#define log_fname_tail   ".out"

const int nfiles = 10;
const size_t log_file_size = (64*MB);


/** Writer thread structure */
struct filewriter_st {
        skygw_chk_t      fwr_chk_top;
        slist_cursor_t*  fwr_logfile_cursor;
        skygw_message_t* fwr_logmes;
        skygw_message_t* fwr_clientmes;
        simple_mutex_t*  fwr_logfile_mutex;
        pthread_t        fwr_tid;
        skygw_thread_t*  fwr_thread;
        skygw_chk_t      fwr_chk_tail;
};

struct logfile_st {
        skygw_chk_t     lf_chk_top;
        logfile_id_t    lf_id;
        char*           lf_directory;
        char*           lf_name_prefix;
        char*           lf_name_tail;
        int             lf_nfiles_max;
        size_t          lf_file_size;
        /** This must be protected */
        slist_cursor_t* lf_writebuf_cursor;
        slist_cursor_t* lf_files_cursor;
        int             lf_npending_writes;
        skygw_chk_t     lf_chk_tail;
};


static bool logfile_write_ex(
        logfile_t* logfile,
        char*      logstr,
        bool       force_flush);


static logfile_t* logfile_init_nomutex(logfile_id_t logfile_id);

static char* get_logfile_directory(
        logfile_id_t logfile_id);

static char* get_logfile_name_prefix(
        logfile_id_t logfile_id);

static char* get_logfile_name_tail(
        logfile_id_t logfile_id);

static int get_logfile_nfiles(
        logfile_id_t logfile_id);

static size_t get_logfile_file_size(
        logfile_id_t logfile_id);

static filewriter_t* filewriter_init(void);

static void* thr_filewriter_fun(void* data);

static skygw_message_t* filewriter_get_logmes(filewriter_t* filewriter);

static skygw_message_t* filewriter_get_clientmes(filewriter_t* filewriter);

bool logfile_write(
        logfile_t* logfile,
        char*      logstr)
{
        CHK_LOGFILE(logfile);
        return logfile_write_ex(logfile, logstr, FALSE);
}

bool logfile_write_flush(
        logfile_t* logfile,
        char*      logstr)
{
        CHK_LOGFILE(logfile);
        return logfile_write_ex(logfile, logstr, TRUE);
}


static bool logfile_write_ex(
        logfile_t* logfile,
        char*      logstr,
        bool       force_flush)
{
        bool succp;
        CHK_LOGFILE(logfile);
#if 0
        /** Attempt to get buffer for log writing */
        lf_buf = logfile_get_buffer(logfile);
        ss_dassert(lf_buf != NULL);
        /**
         * Add string to buffer.
         * Is it possible for this to fail ? */
        succp = logfile_write_to_buf(lf_buf, logstr);

        if (force_flush) {
            /** Send flush message to filewriter */
            logfile_force_flush(logfile);
        }
#else
        succp = TRUE;
#endif
        return succp;
}
            
        
/** 
 * @node Create logfile of a specified type for log writing. 
 *
 * Parameters:
 * @param logfile_id - in, use
 *          Specifies the type of logfile. Types are listed in
 * filewriter.h
 *
 * @return pointer to logfile object. 
 *
 * 
 * @details (write detailed description here)
 *
 */
logfile_t* logfile_init(
        logfile_id_t logfile_id)
{
        filewriter_t* filewriter;
        logfile_t* logfile;

        /** If filewriter doesn't exists, this triggers its creation */
        filewriter = get_or_create_filewriter(logfile_id);
        CHK_FILEWRITER(filewriter);
        /**
           Pitäisikö ensin luoda uusi logfile ja lisätä se sitten
           filewriterin listalle?
           Selkeämpi?
         */
        filewriter_enter_logfilemutex(filewriter);
        
        /** Protected attempt to get logfile if it exists already */
        logfile = filewriter_get_logfile(logfile_id);

        if (logfile == NULL) {
            logfile = logfile_init_nomutex(logfile_id);
        }
        CHK_LOGFILE(logfile);
        filewriter_exit_logfilemutex(filewriter);

        return logfile;
}

static logfile_t* logfile_init_nomutex(
        logfile_id_t logfile_id)
{
        logfile_t* logfile;
        
        logfile = (logfile_t *)malloc(sizeof(logfile_t));
        
        if (logfile == NULL) {
            goto return_with_logfile;
        }

        logfile->lf_chk_top = CHK_NUM_LOGFILE;
        logfile->lf_chk_tail = CHK_NUM_LOGFILE;
        logfile->lf_id = logfile_id;
        logfile->lf_directory = get_logfile_directory(logfile_id);
        logfile->lf_name_prefix = get_logfile_name_prefix(logfile_id);
        logfile->lf_name_tail = get_logfile_name_tail(logfile_id);
        logfile->lf_nfiles_max = get_logfile_nfiles(logfile_id);
        logfile->lf_file_size = get_logfile_file_size(logfile_id);
        /** filewriter reads and removes frop top, clients add to tail */
        logfile->lf_writebuf_cursor = slist_init();
        /** only filewriter reads or modifies */
        logfile->lf_files_cursor = slist_init();
        logfile->lf_npending_writes = 0;
        
return_with_logfile:
        return logfile;
}

/** 
 * @node Filewriter is an object which is managed by file writer thread.
 * A filewriter is returned or - if it doesn't exist - created prior return.
 *
 * Parameters:
 * @param logfile_id - in, use
 *          Logfile id is used only if there are multiple file writer threads.
 *          NOTE that logfile creation is not triggered in this function.
 * 
 *
 * @param WRITER - <usage>
 *          <description>
 *
 * @return pointer to filewriter which is initialized only and so it has no
 * logfile set at this phase.
 *
 * 
 * @details (write detailed description here)
 *
 */
filewriter_t* get_or_create_filewriter(
        logfile_id_t logfile_id /** NOT USED WITH 1 WRITER */)
{
        /** global filewriter pointer */
        static filewriter_t* filewriter;
        static int  a;
        static int  b;
        static bool file_writer_initialized = FALSE;
        int         my_a = 0;
        int         wait_usec;
        bool        just_wait = FALSE;

        while (filewriter == NULL) {
            /** Someone else came before you, wait until filewriter has value */
            if (just_wait) {
                wait_usec = (rand()%10);
                usleep(wait_usec);
                continue;
            }

            if (my_a == a) {
                /** No-one has came since you read a last time, go on */
                
                if (a == b) {
                    a += 1;
                    my_a += 1;
                } else {
                    /** Someone's still in loop, wait until loop is empty */
                    wait_usec = (rand()%10);
                    usleep(wait_usec);
                    continue;
                }
            } else {
                just_wait = TRUE;
                continue;
            }

            if (my_a != a) {
                /** Someone updated a after you. Inc. b and retry. */
                my_a = a;
                b += 1;
                wait_usec = (rand()%100);
                usleep(wait_usec);
                continue;
            }
            
            /** Only one get this far. It is safe to initialize filewriter */
            ss_info_dassert(file_writer_initialized == FALSE,
                            "File writer is already initialized. "
                            "Concurrency problem\n");
            file_writer_initialized = TRUE;
            /**
             * Create filewriter struct and thread to run with it.
             * Wait until thread sends ack.
             */
            filewriter = filewriter_init();
            skygw_message_wait(filewriter->fwr_clientmes);
        }
        CHK_FILEWRITER(filewriter);
        ss_info_dassert(skygw_thread_get_state(filewriter->fwr_thread) ==
                        THR_RUNNING,
                        "File writer thread is not running but filewriter "
                        "is being returned.");
        return filewriter;
}


static filewriter_t* filewriter_init(void)
{
        filewriter_t* filewriter;

        filewriter = (filewriter_t *)malloc(sizeof(filewriter_t));
        filewriter->fwr_chk_top = CHK_NUM_FILEWRITER;
        filewriter->fwr_chk_tail = CHK_NUM_FILEWRITER;
        filewriter->fwr_logfile_cursor = slist_init();
        filewriter->fwr_logmes = skygw_message_init();
        filewriter->fwr_clientmes = skygw_message_init();
        filewriter->fwr_logfile_mutex = simple_mutex_init("logfile");
        filewriter->fwr_thread =
            skygw_thread_init("File writer thr",
                              thr_filewriter_fun,
                              (void *)filewriter);
        skygw_thread_start(filewriter->fwr_thread);
        return filewriter;
}

void filewriter_enter_logfilemutex(
        filewriter_t* fwr)
{
        int i;
        int err;

        for (i=0; i<100; i++) {
            err = simple_mutex_lock(fwr->fwr_logfile_mutex, FALSE);

            if (err == 0) {
                break;
            }
            usleep(200);
        }
        ss_info_dassert(err == 0, "Can't enter logfilemutex");
}

void filewriter_exit_logfilemutex(
        filewriter_t* fwr)
{
        int err;

        err = simple_mutex_unlock(fwr->fwr_logfile_mutex);
        ss_info_dassert(err == 0, "Can't exit logfilemutex");
}


static skygw_message_t* filewriter_get_logmes(
        filewriter_t* filewriter)
{
        CHK_FILEWRITER(filewriter);

        return filewriter->fwr_logmes;
}

static skygw_message_t* filewriter_get_clientmes(
        filewriter_t* filewriter)
{
        CHK_FILEWRITER(filewriter);

        return filewriter->fwr_clientmes;
}

static void* thr_filewriter_fun(
        void* data)
{
        skygw_thread_t* thr;
        filewriter_t*   fwr;
            
        thr = (skygw_thread_t *)data;
        fwr = (filewriter_t *)skygw_thread_get_data(thr);

        skygw_thread_set_state(thr, THR_RUNNING);
        skygw_message_send(fwr->fwr_clientmes);

        while(!skygw_thread_must_exit(thr)) {
            skygw_message_wait(fwr->fwr_logmes);
            /** Read files whose prefix and tail match with those specified in  
             * logfiles and insert names
             */
            /** Do what is needed and inform client then */
            /** Go wait messages from client it timely alarm */
        }
        skygw_thread_set_state(thr, THR_EXIT);
        skygw_message_send(fwr->fwr_clientmes);
        return NULL;
}

logfile_t* filewriter_get_logfile(
        logfile_id_t id)
{
        return NULL;
}

bool filewriter_writebuf(
        filewriter_t* fw,
        void*         buf)
{
        return TRUE;
}

static char* get_logfile_directory(
        logfile_id_t logfile_id)
{
        return "/tmp/";
}

static char* get_logfile_name_prefix(
        logfile_id_t logfile_id)
{
        return "skygw";
}

static char* get_logfile_name_tail(
        logfile_id_t logfile_id)
{
        return ".msg";
}

static int get_logfile_nfiles(
        logfile_id_t logfile_id)
{
        return 3;
}

static size_t get_logfile_file_size(
        logfile_id_t logfile_id)
{
        return 3*KB;
}

void logfile_done(
        logfile_id_t id)
{
        fprintf(stderr, "logfile_done\n");
}

bool logfile_flush(
        logfile_t* logfile)
{
        fprintf(stderr, "logfile_flush\n");
}

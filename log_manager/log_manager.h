

typedef struct filewriter_st  filewriter_t;
typedef struct logfile_st     logfile_t;
typedef struct fnames_conf_st fnames_conf_t;
typedef struct logmanager_st  logmanager_t;

typedef enum {
    LOGFILE_FIRST = 0,
    LOGFILE_TRACE = LOGFILE_FIRST,
    LOGFILE_MESSAGE,
    LOGFILE_ERROR,
    LOGFILE_LAST = LOGFILE_ERROR
} logfile_id_t;

typedef enum { FILEWRITER_INIT, FILEWRITER_RUN, FILEWRITER_DONE }
    filewriter_state_t;
typedef enum { LOGFILE_INIT, LOGFILE_OPENED, LOGFILE_DONE } logfile_state_t;

EXTERN_C_BLOCK_BEGIN

logmanager_t* skygw_logmanager_init(void** ctx, int argc, char* argv[]);
void skygw_logmanager_done(void** ctx, logmanager_t** lm);
int skygw_log_write(void* ctx, logmanager_t* lmgr, logfile_id_t id, char* str);
int skygw_log_flush(logmanager_t* lmgr, logfile_id_t id);
int skygw_log_write_flush(void* ctx,
                          logmanager_t* lmgr,
                          logfile_id_t id,
                          char* str);

EXTERN_C_BLOCK_END

const char* get_trace_prefix_default(void);
const char* get_trace_suffix_default(void);
const char* get_msg_prefix_default(void);
const char* get_msg_suffix_default(void);
const char* get_err_prefix_default(void);
const char* get_err_suffix_default(void);
const char* get_logpath_default(void);

/*
bool logfile_write(
        skygw_ctx_t* ctx,
        logmgr_t*    mgr,
        logfile_id_t id,
        char*        msg);

bool logfile_write_flush(
        skygw_ctx_t* ctx,
        logmgr_t*    mgr,
        logfile_id_t id,
        char*        msg);

bool logfile_flush(
        logmgr_t*    mgr,
        logfile_id_t id);

bool logfile_init(
        logmgr_t*    mgr,
        logfile_id_t id);

void logfile_done(
        logmgr_t*    mgr,
        logfile_id_t id);
*/

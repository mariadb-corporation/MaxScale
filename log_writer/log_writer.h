typedef struct filewriter_st  filewriter_t;

EXTERN_C_BLOCK_BEGIN

typedef enum {
    LOGFILE_TRACE = 0,
    LOGFILE_MESSAGE,
    LOGFILE_ERROR
} logfile_id_t;

typedef struct logfile_st logfile_t;

bool logfile_write(
        logfile_t* logfile,
        char*      logstr);

bool logfile_write_flush(
        logfile_t* logfile,
        char*      logstr);

bool logfile_flush(
        logfile_t* logfile);

logfile_t* logfile_init(
        logfile_id_t logfile_id);

void logfile_done(
        logfile_id_t id);

EXTERN_C_BLOCK_END

filewriter_t* get_or_create_filewriter(
        logfile_id_t logfile_id /** NOT USED WITH 1 WRITER */);

void filewriter_enter_logfilemutex(
        filewriter_t* fwr);

logfile_t* filewriter_get_logfile(logfile_id_t id);

void filewriter_exit_logfilemutex(filewriter_t* fwr);


#include <stdio.h>
#include <string.h>
#include <skygw_utils.h>
#include <log_manager.h>

int main(int argc, char* argv[])
{
        int           err;
        logmanager_t* lmgr;
        char*         logstr;
        
        lmgr = skygw_logmanager_init(NULL, argc, argv);
        
        logstr = strdup("My name is Tracey");
        err = skygw_log_write(NULL, lmgr, LOGFILE_TRACE, logstr);
        free(logstr);
        
        logstr = strdup("My name is Stacey");
        err = skygw_log_write_flush(NULL, lmgr, LOGFILE_TRACE, logstr);
        free(logstr);
        
        skygw_logmanager_done(NULL, &lmgr);

        logstr = strdup("My name is Philip");
        err = skygw_log_write(NULL, lmgr, LOGFILE_TRACE, logstr);
        free(logstr);
        
        lmgr = skygw_logmanager_init(NULL, argc, argv);
        
        logstr = strdup("A terrible error has occurred!");
        err = skygw_log_write_flush(NULL, lmgr, LOGFILE_ERROR, logstr);
        free(logstr);

        logstr = strdup("Hi, how are you?");
        err = skygw_log_write(NULL, lmgr, LOGFILE_MESSAGE, logstr);
        free(logstr);

        logstr = strdup("I'm doing fine!");
        err = skygw_log_write(NULL, lmgr, LOGFILE_MESSAGE, logstr);
        free(logstr);
        
return_err:
        skygw_logmanager_done(NULL, &lmgr);
        return err;
}

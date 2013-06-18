#include <stdio.h>
#include <skygw_utils.h>
#include <log_writer.h>

int main(int argc, char** argv)
{
        logfile_t* tracelog = logfile_init(LOGFILE_TRACE);
        logfile_t* messagelog = logfile_init(LOGFILE_MESSAGE);

        logfile_write(tracelog, "My name is trace");
        logfile_write_flush(messagelog, "I'm the message!");
        logfile_flush(tracelog);

        logfile_done(LOGFILE_TRACE);
        logfile_done(LOGFILE_MESSAGE);
        return 0;
}

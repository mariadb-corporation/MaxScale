#ifndef TEST_UTILS_H
#define TEST_UTILS_H
#include <poll.h>
#include <dcb.h>
#include <housekeeper.h>
#include <maxscale_test.h>
#include <log_manager.h>

void init_test_env(char *path)
{
    int argc = 3;

    const char* logdir = path ? path : TEST_LOG_DIR;

    char* argv[] =
        {
            "log_manager",
            "-l",
            "LOGFILE_ERROR",
            NULL
        };

    skygw_logmanager_init(logdir, argc, argv);
    poll_init();
    hkinit();
}

#endif

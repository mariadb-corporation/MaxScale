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
    
    char* argv[] =
        {
            "log_manager",
            "-j",
            path? path:TEST_LOG_DIR,
            NULL
        };

    skygw_logmanager_init(argc,argv);
    poll_init();
    hkinit();
}

#endif

/**
 * Bad configuration test
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

const char* bad_configs[] =
{
    "bug359",
    "bug495",
    "bug526",
    "bug479",
    "bug493",
    "bad_ssl",
    "mxs710_bad_socket",
    "mxs711_two_ports",
    "mxs720_line_with_no_equal",
    "mxs720_wierd_line",
    "mxs799",
    "mxs1731_empty_param",
    "old_passwd",
    NULL
};

int main(int argc, char** argv)
{
    TestConnections* test = new TestConnections(argc, argv);
    int rval = 0;

    test->maxscales->stop_maxscale(0);

    for (int i = 0; bad_configs[i]; i++)
    {
        printf("Testing %s...\n", bad_configs[i]);
        if (test->test_bad_config(0, bad_configs[i]))
        {
            printf("FAILED\n");
            rval++;
        }
        else
        {
            printf("SUCCESS\n");
        }
    }

    return rval;
}

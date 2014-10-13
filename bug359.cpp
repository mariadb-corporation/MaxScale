#include <my_config.h>
#include <iostream>
//#include <sys/types.h>
//#include <sys/stat.h>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int ReadLog(char * name, char * err_log_content)
{
    FILE *f;
    struct stat buf;



    f = fopen(name,"rb");
    if (f != NULL) {

        int prev=ftell(f);
        fseek(f, 0L, SEEK_END);
        long int size=ftell(f);
        fseek(f, prev, SEEK_SET);
        err_log_content = (char *)malloc(size);
        if (err_log_content != NULL) {
            fread(err_log_content, 1, size, f);
            return(0);
        } else {
            printf("Error allocationg memory for the log\n");
            return(1);
        }
    }
    else {
        printf ("Error reading log %s \n", name);
        return(1);
    }
}

int main()
{

    TestConnections * Test = new TestConnections();
    int global_result = 0;
    char * err_log_content;

    Test->ReadEnv();
    Test->PrintIP();

    global_result = Test->ConnectMaxscale();
    if (global_result != 0) {
        printf("Error opening connections to MaxScale\n");
    }

    system(Test->GetLogsCommand);

    global_result += ReadLog((char *) "skygw_err1.log", err_log_content);

    if (strstr(err_log_content, "Warning : Unsupported router option \"slave\"") == NULL) {
        global_result++;
        printf("There is no \"Warning : Unsupported router option \"slave\" \" warning in the log\n");
    }
    if (strstr(err_log_content, "Error : Couldn't find suitable Master") != NULL) {
        global_result++;
        printf("\"Error : Couldn't find suitable Master\" error is present in the log\n");
    }

    return(global_result);
}

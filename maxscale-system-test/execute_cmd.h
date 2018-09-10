#ifndef EXECUTE_CMD_H
#define EXECUTE_CMD_H

#include <iostream>
#include <unistd.h>

using namespace std;

/**
 * @brief execute_cmd Execute shell command
 * @param cmd Command line
 * @param res Pointer to variable that will contain command console output (stdout)
 * @return Process exit code
 */
int execute_cmd(char* cmd, char** res);

#endif      // EXECUTE_CMD_H

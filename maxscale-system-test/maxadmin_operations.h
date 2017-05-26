#ifndef MAXADMIN_OPERATIONS_H
#define MAXADMIN_OPERATIONS_H


#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <locale.h>
#include <errno.h>
#include <getopt.h>

//#include <version.h>


/**
 * @brief Connect to the MaxScale server
 *
 * @param hostname  The hostname to connect to
 * @param port      The port to use for the connection
 * @return      The connected socket or -1 on error
 */
int connectMaxScale(char *hostname, char *port);

/**
 * @brief Set IP address in socket structure in_addr
 *
 * @param a Pointer to a struct in_addr into which the address is written
 * @param p The hostname to lookup
 * @return  1 on success, 0 on failure
 */
int setipaddress(struct in_addr *a, char *p);


/**
 * @brief Perform authentication using the maxscaled protocol conventions
 *
 * @param so        The socket connected to MaxScale
 * @param user      The username to authenticate
 * @param password  The password to authenticate with
 * @return      Non-zero of succesful authentication
 */
int authMaxScale(int so, char *user, char *password);

/**
 * @brief Send a comamnd using the MaxScaled protocol, display the return data on standard output.
 *
 * Input terminates with a lien containing just the text OK
 *
 * @param so    The socket connect to MaxScale
 * @param cmd   The command to send
 * @return  0 if the connection was closed
 */
int sendCommand(int so, char *cmd, char *buf);


/**
 * @brief Send a comamnd using the MaxScaled protocol, search for certain numeric parameter in MaxScaled output.
 *
 * Input terminates with a lien containing just the text OK
 *
 * @param user      The username to authenticate
 * @param password  The password to authenticate with
 * @param cmd       The command to send
 * @param param     Parameter to find
 * @param result    Value of found parameter
 * @return  0 if parameter is found
 */
int get_maxadmin_param_tcp(char *hostname, char *user, char *password, char *command, char *param,
                           char *result);

/**
 * @brief Send a comamnd using the MaxScaled protocol
 *
 * Input terminates with a line containing just the text OK
 *
 * @param user      The username to authenticate
 * @param password  The password to authenticate with
 * @param cmd       The command to send
 * @return  0 if parameter is found
 */
int execute_maxadmin_command_tcp(char * hostname, char *user, char *password, char * cmd);

/**
 * @brief Send a comamnd using the MaxScaled protocol, print results of stdout
 *
 * Input terminates with a line containing just the text OK
 *
 * @param user      The username to authenticate
 * @param password  The password to authenticate with
 * @param cmd       The command to send
 * @return  0 if parameter is found
 */
int execute_maxadmin_command_print_pcp(char * hostname, char *user, char *password, char * cmd);

#endif // MAXADMIN_OPERATIONS_H

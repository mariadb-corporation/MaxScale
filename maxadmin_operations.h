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


int connectMaxScale(char *hostname, char *port);
int setipaddress(struct in_addr *a, char *p);
int authMaxScale(int so, char *user, char *password);
int sendCommand(int so, char *cmd, char *buf);
int getMaxadminParam(char *hostname, char *user, char *password, char *command, char *param, char *result);
int executeMaxadminCommand(char * hostname, char *user, char *password, char * cmd);

#endif // MAXADMIN_OPERATIONS_H

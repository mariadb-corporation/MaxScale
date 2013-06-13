#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdbool.h>

#define MAX_EVENTS 1000000
#define EXIT_FAILURE 1

// network buffer is 32K
#define MAX_BUFFER_SIZE 32768
// socket send buffer for backend
#define GW_BACKEND_SO_SNDBUF 1024

#define GW_NOINTR_CALL(A)	do { errno = 0; A; } while (errno == EINTR)
#define GW_VERSION "0.1.0"
#define GW_MYSQL_VERSION "5.5.22-SKYSQL-" GW_VERSION
#define GW_MYSQL_LOOP_TIMEOUT 300000000
#define GW_MYSQL_READ 0
#define GW_MYSQL_WRITE 1

#define GW_MYSQL_PROTOCOL_VERSION 10 // version is 10
#define GW_MYSQL_HANDSHAKE_FILLER 0x00
#define GW_MYSQL_SERVER_CAPABILITIES_BYTE1 0xff
#define GW_MYSQL_SERVER_CAPABILITIES_BYTE2 0xf7
#define GW_MYSQL_SERVER_LANGUAGE 0x08
#define GW_MYSQL_MAX_PACKET_LEN 0xffffffL;
#define GW_MYSQL_SCRAMBLE_SIZE 20

// debug for mysql_* functions
#define MYSQL_CONN_DEBUG
#undef MYSQL_CONN_DEBUG

#include "gateway_mysql.h"
#include "mysql_protocol.h"
#include "dcb.h"

int do_read_dcb(DCB *dcb);
int handle_event_errors(DCB *dcb, int event);
int handle_event_errors_backend(DCB *dcb, int event);
void MySQLListener(int epfd, char *config_bind);
int MySQLAccept(DCB *listener, int efd);
int gw_mysql_do_authentication(DCB *dcb, GWBUF *);
void gw_mysql_close(MySQLProtocol **ptr);
char *gw_strend(register const char *s);
int do_read_dcb(DCB *dcb);
int do_read_10(DCB *dcb, uint8_t *buffer);
MySQLProtocol * gw_mysql_init(MySQLProtocol *ptr);

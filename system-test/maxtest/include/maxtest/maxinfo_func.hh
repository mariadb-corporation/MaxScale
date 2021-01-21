#pragma once

#include <cstdlib>

class TestConnections;

int   create_tcp_socket();
char* get_ip(const char* host);

char* read_sc(int sock);
int   send_so(int sock, char* data);
static char hexconvtab[] __attribute__ ((unused)) = "0123456789abcdef";
static char* bin2hex(const unsigned char* old, const size_t oldlen);
char*        cdc_auth_srt(char* user, char* password);
int          setnonblocking(int sock);
int          get_x_fl_from_json(char* line, long long int* x1, long long int* fl);

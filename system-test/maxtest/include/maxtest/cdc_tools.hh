#pragma once

#include <maxtest/ccdefs.hh>

namespace maxtest
{
int   create_tcp_socket();
char* get_ip(const char* host);
char* read_sc(int sock);
int   send_so(int sock, char* data);
char* cdc_auth_srt(char* user, char* password);
int   setnonblocking(int sock);
int   get_x_fl_from_json(const char* line, long long int* x1, long long int* fl);
}

#ifndef MAXINFO_FUNC_H
#define MAXINFO_FUNC_H

int create_tcp_socket();
char *get_ip(char *host);
char *build_get_query(char *host, const char *page);

/**
* @brief get_maxinfo does request to Maxinfo service and return response JSON
* @param page retrived info name
* @param Test TestConnection object
* @return response from Maxinfo
*/
char * get_maxinfo(const char *page, TestConnections* Test);

char * read_sc(int sock);
int send_so(int sock, char * data);
static char hexconvtab[] = "0123456789abcdef";
static char* bin2hex(const unsigned char *old, const size_t oldlen);
char * cdc_auth_srt(char * user, char * password);
int setnonblocking(int sock);
int get_x_fl_from_json(char * line, long long int * x1, long long int * fl);


#endif // MAXINFO_FUNC_H

#include <maxtest/maxinfo_func.hh>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <jansson.h>
#include <openssl/sha.h>
#include <maxtest/testconnections.hh>

using namespace std;

#define PORT      8080
#define USERAGENT "HTMLGET 1.1"

int create_tcp_socket()
{
    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("Can't create TCP socket");
        return 0;
    }
    return sock;
}

char* get_ip(const char* host)
{
    struct hostent* hent;
    int iplen = 16;     // XXX.XXX.XXX.XXX
    char* ip = (char*)malloc(iplen + 1);
    memset(ip, 0, iplen + 1);
    if ((hent = gethostbyname(host)) == NULL)
    {
        herror("Can't get IP");
        return NULL;
    }
    if (inet_ntop(AF_INET, (void*)hent->h_addr_list[0], ip, iplen) == NULL)
    {
        perror("Can't resolve host");
        return NULL;
    }
    return ip;
}

char* read_sc(int sock)
{
    char buf[BUFSIZ + 1];
    int tmpres;
    memset(buf, 0, sizeof(buf));

    char* result = (char*)calloc(BUFSIZ, sizeof(char));
    size_t rsize = sizeof(buf);
    while ((tmpres = recv(sock, buf, BUFSIZ, 0)) > 0)
    {
        result = (char*)realloc(result, tmpres + rsize);
        rsize += tmpres;
        // printf("%s", buf);
        strcat(result, buf);
        memset(buf, 0, tmpres);
    }
    return result;
}
int send_so(int sock, char* data)
{
    int tmpres;
    size_t sent = 0;
    while (sent < strlen(data))
    {
        tmpres = send(sock, data + sent, strlen(data) - sent, 0);
        if (tmpres == -1)
        {
            return -1;
        }
        sent += tmpres;
    }
    return 0;
}

static char* bin2hex(const unsigned char* old, const size_t oldlen)
{
    char* result = (char*) malloc(oldlen * 2 + 1);
    size_t i, j;

    for (i = j = 0; i < oldlen; i++)
    {
        result[j++] = hexconvtab[old[i] >> 4];
        result[j++] = hexconvtab[old[i] & 15];
    }
    result[j] = '\0';
    return result;
}

char* cdc_auth_srt(char* user, char* password)
{
    unsigned char sha1pass[20];
    char* str;
    str = (char*) malloc(42 + strlen(user) * 2 + 1);

    unsigned char* password_u;
    unsigned char* user_u;
    password_u = (unsigned char*) malloc(strlen(password));
    user_u = (unsigned char*) malloc(strlen(user));
    memcpy((void*)password_u, (void*)password, strlen(password));
    memcpy((void*)user_u, (void*)user, strlen(user));

    SHA1(password_u, strlen(password), sha1pass);

    // char * sha1pass_hex = (char *) "454ac34c2999aacfebc6bf5fe9fa1db9b596f625";

    char* sha1pass_hex = bin2hex(sha1pass, 20);
    printf("password %s, len %lu, password sha1: %s\n", password, strlen(password), sha1pass_hex);


    char* user_hex = bin2hex(user_u, strlen(user));
    char* clmn_hex = bin2hex((unsigned char*)":", 1);

    sprintf(str, "%s%s%s", user_hex, clmn_hex, sha1pass_hex);

    free(clmn_hex);
    free(user_hex);
    free(sha1pass_hex);
    free(user_u);
    free(password_u);

    printf("%s\n", str);
    return str;
}

int setnonblocking(int sock)
{
    int opts;
    opts = fcntl(sock, F_GETFL);
    if (opts < 0)
    {
        return -1;
    }
    opts = (opts | O_NONBLOCK);
    if (fcntl(sock, F_SETFL, opts) < 0)
    {
        return -1;
    }
    return 0;
}


int get_x_fl_from_json(char* line, long long int* x1, long long int* fl)
{
    json_t* root;
    json_error_t error;

    root = json_loads(line, 0, &error);
    if (!root)
    {
        fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
        return 1;
    }

    json_t* x_json = json_object_get(root, "x1");
    if (x_json == NULL)
    {
        return 1;
    }
    if (!json_is_integer(x_json))
    {
        printf("x1 is not int, type is %d\n", json_typeof(x_json));
        return 1;
    }

    *x1 = json_integer_value(x_json);
    json_t* fl_json = json_object_get(root, "fl");
    if (fl_json == NULL)
    {
        return 1;
    }
    if (!json_is_integer(fl_json))
    {
        printf("fl is not int\n");
        return 1;
    }

    *fl = json_integer_value(fl_json);
    json_decref(x_json);
    json_decref(fl_json);
    json_decref(root);
    return 0;
}

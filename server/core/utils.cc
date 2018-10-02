/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file utils.c - General utility functions
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 10-06-2013   Massimiliano Pinto      Initial implementation
 * 12-06-2013   Massimiliano Pinto      Read function trought
 *                                      the gwbuff strategy
 * 13-06-2013   Massimiliano Pinto      MaxScale local authentication
 *                                      basics
 * 02-09-2014   Martin Brampton         Replaced C++ comments by C comments
 *
 * @endverbatim
 */

#include <maxscale/utils.h>
#include <maxscale/utils.hh>

#include <fcntl.h>
#include <netdb.h>
#include <regex.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <openssl/sha.h>
#include <curl/curl.h>

#include <maxscale/alloc.h>
#include <maxscale/config.h>
#include <maxscale/dcb.h>
#include <maxscale/log.h>
#include <maxscale/limits.h>
#include <maxscale/pcre2.h>
#include <maxscale/poll.h>
#include <maxscale/random.h>
#include <maxscale/secrets.h>
#include <maxscale/session.h>

#if !defined (PATH_MAX)
# if defined (__USE_POSIX)
#   define PATH_MAX _POSIX_PATH_MAX
# else
#   define PATH_MAX 256
# endif
#endif

#define MAX_ERROR_MSG PATH_MAX

/* used in the hex2bin function */
#define char_val(X) \
    (X >= '0' && X <= '9' ? X - '0'       \
                          : X >= 'A' && X <= 'Z' ? X - 'A' + 10    \
                                                 : X >= 'a' && X <= 'z' ? X - 'a' + 10    \
                                                                        : '\177')

/* used in the bin2hex function */
char hex_upper[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
char hex_lower[] = "0123456789abcdefghijklmnopqrstuvwxyz";

/**
 * Check if the provided pathname is POSIX-compliant. The valid characters
 * are [a-z A-Z 0-9._-].
 * @param path A null-terminated string
 * @return true if it is a POSIX-compliant pathname, otherwise false
 */
bool is_valid_posix_path(char* path)
{
    char* ptr = path;
    while (*ptr != '\0')
    {
        if (isalnum(*ptr) || *ptr == '/' || *ptr == '.' || *ptr == '-' || *ptr == '_')
        {
            ptr++;
        }
        else
        {
            return false;
        }
    }
    return true;
}

/*****************************************
* backend read event triggered by EPOLLIN
*****************************************/

int setnonblocking(int fd)
{
    int fl;

    if ((fl = fcntl(fd, F_GETFL, 0)) == -1)
    {
        MXS_ERROR("Can't GET fcntl for %i, errno = %d, %s.",
                  fd,
                  errno,
                  mxs_strerror(errno));
        return 1;
    }

    if (fcntl(fd, F_SETFL, fl | O_NONBLOCK) == -1)
    {
        MXS_ERROR("Can't SET fcntl for %i, errno = %d, %s",
                  fd,
                  errno,
                  mxs_strerror(errno));
        return 1;
    }
    return 0;
}

int setblocking(int fd)
{
    int fl;

    if ((fl = fcntl(fd, F_GETFL, 0)) == -1)
    {
        MXS_ERROR("Can't GET fcntl for %i, errno = %d, %s.",
                  fd,
                  errno,
                  mxs_strerror(errno));
        return 1;
    }

    if (fcntl(fd, F_SETFL, fl & ~O_NONBLOCK) == -1)
    {
        MXS_ERROR("Can't SET fcntl for %i, errno = %d, %s",
                  fd,
                  errno,
                  mxs_strerror(errno));
        return 1;
    }
    return 0;
}

char* gw_strend(register const char* s)
{
    while (*s++)
    {
    }
    return (char*) (s - 1);
}

/*****************************************
* generate a random char
*****************************************/
static char gw_randomchar()
{
    return (char)((mxs_random() % 78) + 30);
}

/*****************************************
* generate a random string
* output must be pre allocated
*****************************************/
int gw_generate_random_str(char* output, int len)
{
    int i;

    for (i = 0; i < len; ++i)
    {
        output[i] = gw_randomchar();
    }

    output[len] = '\0';

    return 0;
}

/*****************************************
* hex string to binary data
* output must be pre allocated
*****************************************/
int gw_hex2bin(uint8_t* out, const char* in, unsigned int len)
{
    const char* in_end = in + len;

    if (len == 0 || in == NULL)
    {
        return 1;
    }

    while (in < in_end)
    {
        register unsigned char b1 = char_val(*in);
        uint8_t b2 = 0;
        in++;
        b2 = (b1 << 4) | char_val(*in);
        *out++ = b2;

        in++;
    }

    return 0;
}

/*****************************************
* binary data to hex string
* output must be pre allocated
*****************************************/
char* gw_bin2hex(char* out, const uint8_t* in, unsigned int len)
{
    const uint8_t* in_end = in + len;
    if (len == 0 || in == NULL)
    {
        return NULL;
    }

    for (; in != in_end; ++in)
    {
        *out++ = hex_upper[((uint8_t) * in) >> 4];
        *out++ = hex_upper[((uint8_t) * in) & 0x0F];
    }
    *out = '\0';

    return out;
}

/****************************************************
 * fill a preallocated buffer with XOR(str1, str2)
 * XOR between 2 equal len strings
 * note that XOR(str1, XOR(str1 CONCAT str2)) == str2
 * and that  XOR(str1, str2) == XOR(str2, str1)
 *****************************************************/
void gw_str_xor(uint8_t* output, const uint8_t* input1, const uint8_t* input2, unsigned int len)
{
    const uint8_t* input1_end = NULL;
    input1_end = input1 + len;

    while (input1 < input1_end)
    {
        *output++ = *input1++ ^ *input2++;
    }
}

/**********************************************************
* fill a 20 bytes preallocated with SHA1 digest (160 bits)
* for one input on in_len bytes
**********************************************************/
void gw_sha1_str(const uint8_t* in, int in_len, uint8_t* out)
{
    unsigned char hash[SHA_DIGEST_LENGTH];

    SHA1(in, in_len, hash);
    memcpy(out, hash, SHA_DIGEST_LENGTH);
}

/********************************************************
* fill 20 bytes preallocated with SHA1 digest (160 bits)
* for two inputs, in_len and in2_len bytes
********************************************************/
void gw_sha1_2_str(const uint8_t* in, int in_len, const uint8_t* in2, int in2_len, uint8_t* out)
{
    SHA_CTX context;
    unsigned char hash[SHA_DIGEST_LENGTH];

    SHA1_Init(&context);
    SHA1_Update(&context, in, in_len);
    SHA1_Update(&context, in2, in2_len);
    SHA1_Final(hash, &context);

    memcpy(out, hash, SHA_DIGEST_LENGTH);
}


/**
 * node Gets errno corresponding to latest socket error
 *
 * Parameters:
 * @param fd - in, use
 *          socket to examine
 *
 * @return errno
 *
 *
 */
int gw_getsockerrno(int fd)
{
    int eno = 0;
    socklen_t elen = sizeof(eno);

    if (fd <= 0)
    {
        goto return_eno;
    }

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void*)&eno, &elen) != 0)
    {
        eno = 0;
    }

return_eno:
    return eno;
}

/**
 * Create a HEX(SHA1(SHA1(password)))
 *
 * @param password      The password to encrypt
 * @return              The new allocated encrypted password, that the caller must free
 *
 */
char* create_hex_sha1_sha1_passwd(char* passwd)
{
    uint8_t hash1[SHA_DIGEST_LENGTH] = "";
    uint8_t hash2[SHA_DIGEST_LENGTH] = "";
    char* hexpasswd = NULL;

    if ((hexpasswd = (char*)MXS_CALLOC(SHA_DIGEST_LENGTH * 2 + 1, 1)) == NULL)
    {
        return NULL;
    }

    /* hash1 is SHA1(real_password) */
    gw_sha1_str((uint8_t*)passwd, strlen(passwd), hash1);

    /* hash2 is the SHA1(input data), where input_data = SHA1(real_password) */
    gw_sha1_str(hash1, SHA_DIGEST_LENGTH, hash2);

    /* dbpass is the HEX form of SHA1(SHA1(real_password)) */
    gw_bin2hex(hexpasswd, hash2, SHA_DIGEST_LENGTH);

    return hexpasswd;
}

/**
 * Remove duplicate and trailing forward slashes from a path.
 * @param path Path to clean up
 */
bool clean_up_pathname(char* path)
{
    char* data = path;
    size_t len = strlen(path);

    if (len > PATH_MAX)
    {
        MXS_ERROR("Pathname too long: %s", path);
        return false;
    }

    while (*data != '\0')
    {
        if (*data == '/')
        {
            if (*(data + 1) == '/')
            {
                memmove(data, data + 1, len);
                len--;
            }
            else if (*(data + 1) == '\0' && data != path)
            {
                *data = '\0';
            }
            else
            {
                data++;
                len--;
            }
        }
        else
        {
            data++;
            len--;
        }
    }

    return true;
}

/**
 * @brief Internal helper function for mkdir_all()
 *
 * @param path Path to create
 * @param mask Bitmask to use
 * @return True if directory exists or it was successfully created, false on error
 */
static bool mkdir_all_internal(char* path, mode_t mask)
{
    bool rval = false;

    if (mkdir(path, mask) == -1 && errno != EEXIST)
    {
        if (errno == ENOENT)
        {
            /** Try to create the parent directory */
            char* ndir = strrchr(path, '/');
            if (ndir)
            {
                *ndir = '\0';
                if (mkdir_all_internal(path, mask))
                {
                    /** Creation of the parent directory was successful, try to
                     * create the directory again */
                    *ndir = '/';
                    if (mkdir(path, mask) == 0)
                    {
                        rval = true;
                    }
                    else
                    {
                        MXS_ERROR("Failed to create directory '%s': %d, %s",
                                  path,
                                  errno,
                                  mxs_strerror(errno));
                    }
                }
            }
        }
        else
        {
            MXS_ERROR("Failed to create directory '%s': %d, %s",
                      path,
                      errno,
                      mxs_strerror(errno));
        }
    }
    else
    {
        rval = true;
    }

    return rval;
}

/**
 * @brief Create a directory and any parent directories that do not exist
 *
 *
 * @param path Path to create
 * @param mask Bitmask to use
 * @return True if directory exists or it was successfully created, false on error
 */
bool mxs_mkdir_all(const char* path, int mask)
{
    char local_path[strlen(path) + 1];
    strcpy(local_path, path);

    if (local_path[sizeof(local_path) - 2] == '/')
    {
        local_path[sizeof(local_path) - 2] = '\0';
    }

    return mkdir_all_internal(local_path, (mode_t)mask);
}

char* trim_leading(char* str)
{
    char* ptr = str;

    while (isspace(*ptr))
    {
        ptr++;
    }

    if (ptr != str)
    {
        memmove(str, ptr, strlen(ptr) + 1);
    }

    return str;
}

char* trim_trailing(char* str)
{
    char* ptr = strchr(str, '\0') - 1;

    while (ptr > str && isspace(*ptr))
    {
        ptr--;
    }

    if (isspace(*(ptr + 1)))
    {
        *(ptr + 1) = '\0';
    }

    return str;
}

char* trim(char* str)
{
    return trim_leading(trim_trailing(str));
}

/**
 * @brief Replace whitespace with hyphens
 *
 * @param str String to replace
 */
void replace_whitespace(char* str)
{
    for (char* s = str; *s; s++)
    {
        if (isspace(*s))
        {
            *s = '-';
        }
    }
}

/**
 * Replace all whitespace with spaces and squeeze repeating whitespace characters
 *
 * @param str String to squeeze
 * @return Squeezed string
 */
char* squeeze_whitespace(char* str)
{
    char* store = str;
    char* ptr = str;

    /** Remove leading whitespace */
    while (isspace(*ptr) && *ptr != '\0')
    {
        ptr++;
    }

    /** Squeeze all repeating whitespace */
    while (*ptr != '\0')
    {
        while (isspace(*ptr) && isspace(*(ptr + 1)))
        {
            ptr++;
        }

        if (isspace(*ptr))
        {
            *store++ = ' ';
            ptr++;
        }
        else
        {
            *store++ = *ptr++;
        }
    }

    *store = '\0';

    /** Remove trailing whitespace */
    while (store > str && isspace(*(store - 1)))
    {
        store--;
        *store = '\0';
    }

    return str;
}

/**
 * Strip escape characters from a character string.
 * @param String to parse.
 * @return True if parsing was successful, false on errors.
 */
bool strip_escape_chars(char* val)
{
    int cur, end;

    if (val == NULL)
    {
        return false;
    }

    end = strlen(val) + 1;
    cur = 0;

    while (cur < end)
    {
        if (val[cur] == '\\')
        {
            memmove(val + cur, val + cur + 1, end - cur - 1);
            end--;
        }
        cur++;
    }
    return true;
}

#define BUFFER_GROWTH_RATE 2.0
static pcre2_code* remove_comments_re = NULL;
static const PCRE2_SPTR remove_comments_pattern = (PCRE2_SPTR)
    "(?:`[^`]*`\\K)|"
    "(\\/[*](?!(M?!)).*?[*]\\/)|"
    "((?:#.*|--[[:space:]].*)(\\n|\\r\\n|$))";

/**
 * Remove SQL comments from the end of a string
 *
 * The inline executable comments are not removed due to the fact that they can
 * alter the behavior of the query.
 * @param src Pointer to the string to modify.
 * @param srcsize Pointer to a size_t variable which holds the length of the string to
 * be modified.
 * @param dest The address of the pointer where the result will be stored. If the
 * value pointed by this parameter is NULL, new memory will be allocated as needed.
 * @param Pointer to a size_t variable where the size of the result string is stored.
 * @return Pointer to new modified string or NULL if memory allocation failed.
 * If NULL is returned and the value pointed by @c dest was not NULL, no new
 * memory will be allocated, the memory pointed by @dest will be freed and the
 * contents of @c dest and @c destsize will be invalid.
 */
char* remove_mysql_comments(const char** src, const size_t* srcsize, char** dest, size_t* destsize)
{
    static const PCRE2_SPTR replace = (PCRE2_SPTR) "";
    pcre2_match_data* mdata;
    char* output = *dest;
    size_t orig_len = *srcsize;
    size_t len = output ? *destsize : orig_len;
    if (orig_len > 0)
    {
        if ((output || (output = (char*) malloc(len * sizeof(char))))
            && (mdata = pcre2_match_data_create_from_pattern(remove_comments_re, NULL)))
        {
            size_t len_tmp = len;
            while (pcre2_substitute(remove_comments_re,
                                    (PCRE2_SPTR) * src,
                                    orig_len,
                                    0,
                                    PCRE2_SUBSTITUTE_GLOBAL,
                                    mdata,
                                    NULL,
                                    replace,
                                    PCRE2_ZERO_TERMINATED,
                                    (PCRE2_UCHAR8*) output,
                                    &len_tmp) == PCRE2_ERROR_NOMEMORY)
            {
                len_tmp = (size_t) (len * BUFFER_GROWTH_RATE + 1);
                char* tmp = (char*) realloc(output, len_tmp);
                if (tmp == NULL)
                {
                    free(output);
                    output = NULL;
                    break;
                }
                output = tmp;
                len = len_tmp;
            }
            pcre2_match_data_free(mdata);
        }
        else
        {
            free(output);
            output = NULL;
        }
    }
    else if (output == NULL)
    {
        output = strdup(*src);
    }

    if (output)
    {
        *destsize = strlen(output);
        *dest = output;
    }

    return output;
}

static pcre2_code* replace_values_re = NULL;
static const PCRE2_SPTR replace_values_pattern = (PCRE2_SPTR) "(?i)([-=,+*/([:space:]]|\\b|[@])"
                                                              "(?:[0-9.-]+|(?<=[@])[a-z_0-9]+)([-=,+*/)[:space:];]|$)";

/**
 * Replace literal numbers and user variables with a question mark.
 * @param src Pointer to the string to modify.
 * @param srcsize Pointer to a size_t variable which holds the length of the string to
 * be modified.
 * @param dest The address of the pointer where the result will be stored. If the
 * value pointed by this parameter is NULL, new memory will be allocated as needed.
 * @param Pointer to a size_t variable where the size of the result string is stored.
 * @return Pointer to new modified string or NULL if memory allocation failed.
 * If NULL is returned and the value pointed by @c dest was not NULL, no new
 * memory will be allocated, the memory pointed by @dest will be freed and the
 * contents of @c dest and @c destsize will be invalid.
 */
char* replace_values(const char** src, const size_t* srcsize, char** dest, size_t* destsize)
{
    static const PCRE2_SPTR replace = (PCRE2_SPTR) "$1?$2";
    pcre2_match_data* mdata;
    char* output = *dest;
    size_t orig_len = *srcsize;
    size_t len = output ? *destsize : orig_len;

    if (orig_len > 0)
    {
        if ((output || (output = (char*) malloc(len * sizeof(char))))
            && (mdata = pcre2_match_data_create_from_pattern(replace_values_re, NULL)))
        {
            size_t len_tmp = len;
            while (pcre2_substitute(replace_values_re,
                                    (PCRE2_SPTR) * src,
                                    orig_len,
                                    0,
                                    PCRE2_SUBSTITUTE_GLOBAL,
                                    mdata,
                                    NULL,
                                    replace,
                                    PCRE2_ZERO_TERMINATED,
                                    (PCRE2_UCHAR8*) output,
                                    &len_tmp) == PCRE2_ERROR_NOMEMORY)
            {
                len_tmp = (size_t) (len * BUFFER_GROWTH_RATE + 1);
                char* tmp = (char*) realloc(output, len_tmp);
                if (tmp == NULL)
                {
                    free(output);
                    output = NULL;
                    break;
                }
                output = tmp;
                len = len_tmp;
            }
            pcre2_match_data_free(mdata);
        }
        else
        {
            free(output);
            output = NULL;
        }
    }
    else if (output == NULL)
    {
        output = strdup(*src);
    }

    if (output)
    {
        *destsize = strlen(output);
        *dest = output;
    }

    return output;
}

/**
 * Find the given needle - user-provided literal -  and replace it with
 * replacement string. Separate user-provided literals from matching table names
 * etc. by searching only substrings preceded by non-letter and non-number.
 *
 * @param haystack      Plain text query string, not to be freed
 * @param needle        Substring to be searched, not to be freed
 * @param replacement   Replacement text, not to be freed
 *
 * @return newly allocated string where needle is replaced
 */
char* replace_literal(char* haystack, const char* needle, const char* replacement)
{
    const char* prefix = "[ ='\",\\(]";     /*< ' ','=','(',''',''"',',' are allowed before needle */
    const char* suffix = "([^[:alnum:]]|$)";/*< alpha-num chars aren't allowed after the needle */
    char* search_re;
    char* newstr;
    regex_t re;
    regmatch_t match;
    int rc;
    size_t rlen = strlen(replacement);
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);

    search_re = (char*) malloc(strlen(prefix) + nlen + strlen(suffix) + 1);

    if (search_re == NULL)
    {
        fprintf(stderr,
                "Regex memory allocation failed : %s\n",
                mxs_strerror(errno));
        newstr = haystack;
        goto retblock;
    }

    sprintf(search_re, "%s%s%s", prefix, needle, suffix);
    /** Allocate memory for new string +1 for terminating byte */
    newstr = (char*) malloc(hlen - nlen + rlen + 1);

    if (newstr == NULL)
    {
        fprintf(stderr,
                "Regex memory allocation failed : %s\n",
                mxs_strerror(errno));
        free(search_re);
        free(newstr);
        newstr = haystack;
        goto retblock;
    }

    rc = regcomp(&re, search_re, REG_EXTENDED | REG_ICASE);
    mxb_assert_message(rc == 0, "Regex check");

    if (rc != 0)
    {
        char error_message[MAX_ERROR_MSG];
        regerror(rc, &re, error_message, MAX_ERROR_MSG);
        fprintf(stderr,
                "Regex error compiling '%s': %s\n",
                search_re,
                error_message);
        free(search_re);
        free(newstr);
        newstr = haystack;
        goto retblock;
    }
    rc = regexec(&re, haystack, 1, &match, 0);

    if (rc != 0)
    {
        free(search_re);
        free(newstr);
        regfree(&re);
        newstr = haystack;
        goto retblock;
    }
    memcpy(newstr, haystack, match.rm_so + 1);
    memcpy(newstr + match.rm_so + 1, replacement, rlen);
    /** +1 is terminating byte */
    memcpy(newstr + match.rm_so + 1 + rlen,
           haystack + match.rm_so + 1 + nlen,
           hlen - (match.rm_so + 1) - nlen + 1);

    regfree(&re);
    free(haystack);
    free(search_re);
retblock:
    return newstr;
}

static pcre2_code* replace_quoted_re = NULL;
static const PCRE2_SPTR replace_quoted_pattern = (PCRE2_SPTR)
    "(?>[^'\"]*)(?|(?:\"\\K(?:(?:(?<=\\\\)\")|[^\"])*(\"))|(?:'\\K(?:(?:(?<=\\\\)')|[^'])*(')))";

/**
 * Replace contents of single or double quoted strings with question marks.
 * @param src Pointer to the string to modify.
 * @param srcsize Pointer to a size_t variable which holds the length of the string to
 * be modified.
 * @param dest The address of the pointer where the result will be stored. If the
 * value pointed by this parameter is NULL, new memory will be allocated as needed.
 * @param Pointer to a size_t variable where the size of the result string is stored.
 * @return Pointer to new modified string or NULL if memory allocation failed.
 * If NULL is returned and the value pointed by @c dest was not NULL, no new
 * memory will be allocated, the memory pointed by @dest will be freed and the
 * contents of @c dest and @c destsize will be invalid.
 */
char* replace_quoted(const char** src, const size_t* srcsize, char** dest, size_t* destsize)
{
    static const PCRE2_SPTR replace = (PCRE2_SPTR) "?$1";
    pcre2_match_data* mdata;
    char* output = *dest;
    size_t orig_len = *srcsize;
    size_t len = output ? *destsize : orig_len;

    if (orig_len > 0)
    {
        if ((output || (output = (char*) malloc(len * sizeof(char))))
            && (mdata = pcre2_match_data_create_from_pattern(replace_quoted_re, NULL)))
        {
            size_t len_tmp = len;
            while (pcre2_substitute(replace_quoted_re,
                                    (PCRE2_SPTR) * src,
                                    orig_len,
                                    0,
                                    PCRE2_SUBSTITUTE_GLOBAL,
                                    mdata,
                                    NULL,
                                    replace,
                                    PCRE2_ZERO_TERMINATED,
                                    (PCRE2_UCHAR8*) output,
                                    &len_tmp) == PCRE2_ERROR_NOMEMORY)
            {
                len_tmp = (size_t) (len * BUFFER_GROWTH_RATE + 1);
                char* tmp = (char*) realloc(output, len_tmp);
                if (tmp == NULL)
                {
                    free(output);
                    output = NULL;
                    break;
                }
                output = tmp;
                len = len_tmp;
            }
            pcre2_match_data_free(mdata);
        }
        else
        {
            free(output);
            output = NULL;
        }
    }
    else if (output == NULL)
    {
        output = strdup(*src);
    }

    if (output)
    {
        *destsize = strlen(output);
        *dest = output;
    }
    else
    {
        *dest = NULL;
    }

    return output;
}

/**
 * Initialize the utils library
 *
 * This function initializes structures used in various functions.
 * @return true on success, false on error
 */
bool utils_init()
{
    bool rval = true;

    PCRE2_SIZE erroffset;
    int errcode;

    mxb_assert_message(remove_comments_re == NULL, "utils_init called multiple times");
    remove_comments_re = pcre2_compile(remove_comments_pattern,
                                       PCRE2_ZERO_TERMINATED,
                                       0,
                                       &errcode,
                                       &erroffset,
                                       NULL);
    if (remove_comments_re == NULL)
    {
        rval = false;
    }

    mxb_assert_message(replace_quoted_re == NULL, "utils_init called multiple times");
    replace_quoted_re = pcre2_compile(replace_quoted_pattern,
                                      PCRE2_ZERO_TERMINATED,
                                      0,
                                      &errcode,
                                      &erroffset,
                                      NULL);
    if (replace_quoted_re == NULL)
    {
        rval = false;
    }

    mxb_assert_message(replace_values_re == NULL, "utils_init called multiple times");
    replace_values_re = pcre2_compile(replace_values_pattern,
                                      PCRE2_ZERO_TERMINATED,
                                      0,
                                      &errcode,
                                      &erroffset,
                                      NULL);
    if (replace_values_re == NULL)
    {
        rval = false;
    }

    return rval;
}

/**
 * Close the utils library. This should be the last call to this library.
 */
void utils_end()
{
    pcre2_code_free(remove_comments_re);
    remove_comments_re = NULL;
    pcre2_code_free(replace_quoted_re);
    replace_quoted_re = NULL;
    pcre2_code_free(replace_values_re);
    replace_values_re = NULL;
}

bool configure_network_socket(int so, int type)
{
    int sndbufsize = MXS_SO_SNDBUF_SIZE;
    int rcvbufsize = MXS_SO_RCVBUF_SIZE;
    int one = 1;

    if (setsockopt(so, SOL_SOCKET, SO_SNDBUF, &sndbufsize, sizeof(sndbufsize)) != 0
        || setsockopt(so, SOL_SOCKET, SO_RCVBUF, &rcvbufsize, sizeof(rcvbufsize)) != 0
        || (type != AF_UNIX && setsockopt(so, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0))
    {
        MXS_ERROR("Failed to set socket option: %d, %s.", errno, mxs_strerror(errno));
        mxb_assert(!true);
        return false;
    }

    return setnonblocking(so) == 0;
}

static bool configure_listener_socket(int so)
{
    int one = 1;

    if (setsockopt(so, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0
        || setsockopt(so, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0)
    {
        MXS_ERROR("Failed to set socket option: %d, %s.", errno, mxs_strerror(errno));
        return false;
    }

    return setnonblocking(so) == 0;
}

static void set_port(struct sockaddr_storage* addr, uint16_t port)
{
    if (addr->ss_family == AF_INET)
    {
        struct sockaddr_in* ip = (struct sockaddr_in*)addr;
        ip->sin_port = htons(port);
    }
    else if (addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6* ip = (struct sockaddr_in6*)addr;
        ip->sin6_port = htons(port);
    }
    else
    {
        MXS_ERROR("Unknown address family: %d", (int)addr->ss_family);
        mxb_assert(false);
    }
}

int open_network_socket(enum mxs_socket_type type,
                        struct sockaddr_storage* addr,
                        const char* host,
                        uint16_t port)
{
    mxb_assert(type == MXS_SOCKET_NETWORK || type == MXS_SOCKET_LISTENER);
    struct addrinfo* ai = NULL, hint = {};
    int so = 0, rc = 0;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_family = AF_UNSPEC;
    hint.ai_flags = AI_ALL;

    if ((rc = getaddrinfo(host, NULL, &hint, &ai)) != 0)
    {
        MXS_ERROR("Failed to obtain address for host %s: %s", host, gai_strerror(rc));
        return -1;
    }

    /* Take the first one */
    if (ai)
    {
        if ((so = socket(ai->ai_family, SOCK_STREAM, 0)) == -1)
        {
            MXS_ERROR("Socket creation failed: %d, %s.", errno, mxs_strerror(errno));
        }
        else
        {
            memcpy(addr, ai->ai_addr, ai->ai_addrlen);
            set_port(addr, port);

            freeaddrinfo(ai);

            if ((type == MXS_SOCKET_NETWORK && !configure_network_socket(so, addr->ss_family))
                || (type == MXS_SOCKET_LISTENER && !configure_listener_socket(so)))
            {
                close(so);
                so = -1;
            }
            else if (type == MXS_SOCKET_LISTENER && bind(so, (struct sockaddr*)addr, sizeof(*addr)) < 0)
            {
                MXS_ERROR("Failed to bind on '%s:%u': %d, %s",
                          host,
                          port,
                          errno,
                          mxs_strerror(errno));
                close(so);
                so = -1;
            }
            else if (type == MXS_SOCKET_NETWORK)
            {
                MXS_CONFIG* config = config_get_global_options();

                if (config->local_address)
                {
                    if ((rc = getaddrinfo(config->local_address, NULL, &hint, &ai)) == 0)
                    {
                        struct sockaddr_storage local_address = {};

                        memcpy(&local_address, ai->ai_addr, ai->ai_addrlen);
                        freeaddrinfo(ai);

                        if (bind(so, (struct sockaddr*)&local_address, sizeof(local_address)) == 0)
                        {
                            MXS_INFO("Bound connecting socket to \"%s\".", config->local_address);
                        }
                        else
                        {
                            MXS_ERROR("Could not bind connecting socket to local address \"%s\", "
                                      "connecting to server using default local address: %s",
                                      config->local_address,
                                      mxs_strerror(errno));
                        }
                    }
                    else
                    {
                        MXS_ERROR("Could not get address information for local address \"%s\", "
                                  "connecting to server using default local address: %s",
                                  config->local_address,
                                  mxs_strerror(errno));
                    }
                }
            }
        }
    }

    return so;
}

static bool configure_unix_socket(int so)
{
    int one = 1;

    if (setsockopt(so, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0)
    {
        MXS_ERROR("Failed to set socket option: %d, %s.", errno, mxs_strerror(errno));
        return false;
    }

    return setnonblocking(so) == 0;
}

int open_unix_socket(enum mxs_socket_type type, struct sockaddr_un* addr, const char* path)
{
    int fd = -1;

    if (strlen(path) > sizeof(addr->sun_path) - 1)
    {
        MXS_ERROR("The path %s specified for the UNIX domain socket is too long. "
                  "The maximum length is %lu.",
                  path,
                  sizeof(addr->sun_path) - 1);
    }
    else if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
    {
        MXS_ERROR("Can't create UNIX socket: %d, %s", errno, mxs_strerror(errno));
    }
    else if (configure_unix_socket(fd))
    {
        addr->sun_family = AF_UNIX;
        strcpy(addr->sun_path, path);

        /* Bind the socket to the Unix domain socket */
        if (type == MXS_SOCKET_LISTENER && bind(fd, (struct sockaddr*)addr, sizeof(*addr)) < 0)
        {
            MXS_ERROR("Failed to bind to UNIX Domain socket '%s': %d, %s",
                      path,
                      errno,
                      mxs_strerror(errno));
            close(fd);
            fd = -1;
        }
    }

    return fd;
}

long get_processor_count()
{
    long processors = 1;
#ifdef _SC_NPROCESSORS_ONLN
    if ((processors = sysconf(_SC_NPROCESSORS_ONLN)) <= 0)
    {
        MXS_WARNING("Unable to establish the number of available cores. Defaulting to 1.");
        processors = 1;
    }
#else
#error _SC_NPROCESSORS_ONLN not available.
#endif
    return processors;
}

int64_t get_total_memory()
{
    int64_t pagesize = 0;
    int64_t num_pages = 0;
#if defined _SC_PAGESIZE && defined _SC_PHYS_PAGES
    if ((pagesize = sysconf(_SC_PAGESIZE)) <= 0 || (num_pages = sysconf(_SC_PHYS_PAGES)) <= 0)
    {
        MXS_WARNING("Unable to establish total system memory");
        pagesize = 0;
        num_pages = 0;
    }
#else
#error _SC_PAGESIZE and _SC_PHYS_PAGES are not defined
#endif
    mxb_assert(pagesize * num_pages > 0);
    return pagesize * num_pages;
}

namespace maxscale
{

std::string to_hex(uint8_t value)
{
    std::string out;
    out += hex_lower[value >> 4];
    out += hex_lower[value & 0x0F];
    return out;
}

uint64_t get_byteN(const uint8_t* ptr, int bytes)
{
    uint64_t rval = 0;
    mxb_assert(bytes >= 0 && bytes <= (int)sizeof(rval));
    for (int i = 0; i < bytes; i++)
    {
        rval += (uint64_t)ptr[i] << (i * 8);
    }
    return rval;
}

uint8_t* set_byteN(uint8_t* ptr, uint64_t value, int bytes)
{
    mxb_assert(bytes >= 0 && bytes <= (int)sizeof(value));
    for (int i = 0; i < bytes; i++)
    {
        ptr[i] = (uint8_t)(value >> (i * 8));
    }
    return ptr + bytes;
}

std::string string_printf(const char* format, ...)
{
    /* Use 'vsnprintf' for the formatted printing. It outputs the optimal buffer length - 1. */
    va_list args;
    va_start(args, format);
    int characters = vsnprintf(NULL, 0, format, args);
    va_end(args);
    std::string rval;
    if (characters < 0)
    {
        // Encoding (programmer) error.
        mxb_assert(!true);
        MXS_ERROR("Could not format the string %s.", format);
    }
    else if (characters > 0)
    {
        // 'characters' does not include the \0-byte.
        int total_size = characters + 1;
        rval.reserve(total_size);
        rval.resize(characters);    // The final "length" of the string
        va_start(args, format);
        // Write directly to the string internal array, avoiding any temporary arrays.
        vsnprintf(&rval[0], total_size, format, args);
        va_end(args);
    }
    return rval;
}

namespace
{

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    std::string* buf = static_cast<std::string*>(userdata);

    if (nmemb > 0)
    {
        buf->append(ptr, nmemb);
    }

    return nmemb;
}

size_t header_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    std::unordered_map<std::string, std::string>* map =
        static_cast<std::unordered_map<std::string, std::string>*>(userdata);

    if (nmemb > 0)
    {
        std::string data(ptr, size* nmemb);
        auto pos = data.find_first_of(':');

        if (pos != std::string::npos)
        {
            std::string key = data.substr(0, pos);
            std::string value = data.substr(pos + 1);
            trim(key);
            trim(value);
            map->insert(std::make_pair(key, value));
        }
    }

    return nmemb * size;
}
}

namespace http
{

Result get(const std::string& url, const std::string& user, const std::string& password)
{
    Result res;
    char errbuf[CURL_ERROR_SIZE + 1] = "";
    CURL* curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10); // For connection phase
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);        // For data transfer phase
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.raw_body);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res.headers);

    if (!user.empty() && !password.empty())
    {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERPWD, (user + ":" + password).c_str());
    }

    long code = 0;      // needs to be a long

    if (curl_easy_perform(curl) == CURLE_OK)
    {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        res.code = code;
    }
    else
    {
        res.code = -1;
        res.raw_body = errbuf;
    }

    // Even the errors are valid JSON so this should be OK
    json_error_t err;
    res.body.reset(json_loads(res.raw_body.c_str(), 0, &err));

    curl_easy_cleanup(curl);

    return std::move(res);
}
}
}

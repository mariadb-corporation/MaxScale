/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
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


#include <gw.h>
#include <dcb.h>
#include <session.h>
#include <openssl/sha.h>
#include <maxscale/alloc.h>
#include <maxscale/poll.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <secrets.h>
#include <random_jkiss.h>

/* used in the hex2bin function */
#define char_val(X) (X >= '0' && X <= '9' ? X-'0' :     \
                     X >= 'A' && X <= 'Z' ? X-'A'+10 :  \
                     X >= 'a' && X <= 'z' ? X-'a'+10 :  \
                     '\177')

/* used in the bin2hex function */
char hex_upper[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
char hex_lower[] = "0123456789abcdefghijklmnopqrstuvwxyz";

/*****************************************
 * backend read event triggered by EPOLLIN
 *****************************************/

int setnonblocking(int fd)
{
    int fl;

    if ((fl = fcntl(fd, F_GETFL, 0)) == -1)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Can't GET fcntl for %i, errno = %d, %s.",
                  fd,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        return 1;
    }

    if (fcntl(fd, F_SETFL, fl | O_NONBLOCK) == -1)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Can't SET fcntl for %i, errno = %d, %s",
                  fd,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        return 1;
    }
    return 0;
}


char *gw_strend(register const char *s)
{
    while (*s++)
    {
        ;
    }
    return (char*) (s - 1);
}

/*****************************************
 * generate a random char
 *****************************************/
static char gw_randomchar()
{
    return (char)((random_jkiss() % 78) + 30);
}

/*****************************************
 * generate a random string
 * output must be pre allocated
 *****************************************/
int gw_generate_random_str(char *output, int len)
{
    int i;

    for (i = 0; i < len; ++i )
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
int gw_hex2bin(uint8_t *out, const char *in, unsigned int len)
{
    const char *in_end = in + len;

    if (len == 0 || in == NULL)
    {
        return 1;
    }

    while (in < in_end)
    {
        register unsigned char b1 = char_val(*in);
        uint8_t b2 = 0;
        in++;
        b2 =  (b1 << 4) | char_val(*in);
        *out++ = b2;

        in++;
    }

    return 0;
}

/*****************************************
 * binary data to hex string
 * output must be pre allocated
 *****************************************/
char *gw_bin2hex(char *out, const uint8_t *in, unsigned int len)
{
    const uint8_t *in_end = in + len;
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
void gw_str_xor(uint8_t *output, const uint8_t *input1, const uint8_t *input2, unsigned int len)
{
    const uint8_t *input1_end = NULL;
    input1_end = input1 + len;

    while (input1 < input1_end)
    {
        *output++ = *input1++ ^ *input2++;
    }

    *output = '\0';
}

/**********************************************************
 * fill a 20 bytes preallocated with SHA1 digest (160 bits)
 * for one input on in_len bytes
 **********************************************************/
void gw_sha1_str(const uint8_t *in, int in_len, uint8_t *out)
{
    unsigned char hash[SHA_DIGEST_LENGTH];

    SHA1(in, in_len, hash);
    memcpy(out, hash, SHA_DIGEST_LENGTH);
}

/********************************************************
 * fill 20 bytes preallocated with SHA1 digest (160 bits)
 * for two inputs, in_len and in2_len bytes
 ********************************************************/
void gw_sha1_2_str(const uint8_t *in, int in_len, const uint8_t *in2, int in2_len, uint8_t *out)
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

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&eno, &elen) != 0)
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
char *create_hex_sha1_sha1_passwd(char *passwd)
{
    uint8_t hash1[SHA_DIGEST_LENGTH] = "";
    uint8_t hash2[SHA_DIGEST_LENGTH] = "";
    char *hexpasswd = NULL;

    if ((hexpasswd = (char *)MXS_CALLOC(SHA_DIGEST_LENGTH * 2 + 1, 1)) == NULL)
    {
        return NULL;
    }

    /* hash1 is SHA1(real_password) */
    gw_sha1_str((uint8_t *)passwd, strlen(passwd), hash1);

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
void clean_up_pathname(char *path)
{
    char *data = path;
    size_t len = strlen(path);

    if (len > PATH_MAX)
    {
        MXS_WARNING("Pathname too long: %s", path);
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
}

/**
 * @brief Internal helper function for mkdir_all()
 *
 * @param path Path to create
 * @param mask Bitmask to use
 * @return True if directory exists or it was successfully created, false on error
 */
static bool mkdir_all_internal(char *path, mode_t mask)
{
    bool rval = false;

    if (mkdir(path, mask) == -1 && errno != EEXIST)
    {
        if (errno == ENOENT)
        {
            /** Try to create the parent directory */
            char *ndir = strrchr(path, '/');
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
                        char err[STRERROR_BUFLEN];
                        MXS_ERROR("Failed to create directory '%s': %d, %s",
                                  path, errno, strerror_r(errno, err, sizeof(err)));
                    }
                }
            }
        }
        else
        {
            char err[STRERROR_BUFLEN];
            MXS_ERROR("Failed to create directory '%s': %d, %s",
                      path, errno, strerror_r(errno, err, sizeof(err)));
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
bool mxs_mkdir_all(const char *path, int mask)
{
    char local_path[strlen(path) + 1];
    strcpy(local_path, path);

    if (local_path[sizeof(local_path) - 2] == '/')
    {
        local_path[sizeof(local_path) - 2] = '\0';
    }

    return mkdir_all_internal(local_path, (mode_t)mask);
}

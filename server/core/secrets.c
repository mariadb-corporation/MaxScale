/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */

#include <secrets.h>
#include <time.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <ctype.h>
#include <mysql_client_server_protocol.h>
#include <gwdirs.h>
#include <random_jkiss.h>

/**
 * Generate a random printable character
 *
 * @return A random printable character
 */
static unsigned char
secrets_randomchar()
{
    return(char) ((random_jkiss() % ('~' - ' ')) + ' ');
}

static int
secrets_random_str(unsigned char *output, int len)
{
    for (int i = 0; i < len; ++i)
    {
        output[i] = secrets_randomchar();
    }
    return 0;
}

/**
 * This routine reads data from a binary file named ".secrets" and extracts the AES encryption key
 * and the AES Init Vector.
 * If the path parameter is not null the custom path is interpreted as a folder
 * containing the .secrets file. Otherwise the default location is used.
 * @return  The keys structure or NULL on error
 */
static MAXKEYS *
secrets_readKeys(const char* path)
{
    char secret_file[PATH_MAX + 1];
    char *home;
    MAXKEYS *keys;
    struct stat secret_stats;
    int fd;
    int len;
    static int reported = 0;

    if (path != NULL)
    {
        snprintf(secret_file, PATH_MAX, "%s/.secrets", path);
    }
    else
    {
        snprintf(secret_file, PATH_MAX, "%s/.secrets", get_datadir());
    }
    /* Try to access secrets file */
    if (access(secret_file, R_OK) == -1)
    {
        int eno = errno;
        errno = 0;
        if (eno == ENOENT)
        {
            if (!reported)
            {
                char errbuf[STRERROR_BUFLEN];
                MXS_NOTICE("Encrypted password file %s can't be accessed "
                           "(%s). Password encryption is not used.",
                           secret_file,
                           strerror_r(eno, errbuf, sizeof(errbuf)));
                reported = 1;
            }
        }
        else
        {
            char errbuf[STRERROR_BUFLEN];
            MXS_ERROR("Access for secrets file "
                      "[%s] failed. Error %d, %s.",
                      secret_file,
                      eno,
                      strerror_r(eno, errbuf, sizeof(errbuf)));
        }
        return NULL;
    }

    /* open secret file */
    if ((fd = open(secret_file, O_RDONLY)) < 0)
    {
        int eno = errno;
        errno = 0;
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Failed opening secret "
                  "file [%s]. Error %d, %s.",
                  secret_file,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        return NULL;

    }

    /* accessing file details */
    if (fstat(fd, &secret_stats) < 0)
    {
        int eno = errno;
        errno = 0;
        close(fd);
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("fstat for secret file %s "
                  "failed. Error %d, %s.",
                  secret_file,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        return NULL;
    }

    if (secret_stats.st_size != sizeof(MAXKEYS))
    {
        int eno = errno;
        errno = 0;
        close(fd);
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Secrets file %s has "
                  "incorrect size. Error %d, %s.",
                  secret_file,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        return NULL;
    }
    if (secret_stats.st_mode != (S_IRUSR | S_IFREG))
    {
        close(fd);
        MXS_ERROR("Ignoring secrets file "
                  "%s, invalid permissions.",
                  secret_file);
        return NULL;
    }

    if ((keys = (MAXKEYS *) malloc(sizeof(MAXKEYS))) == NULL)
    {
        close(fd);
        MXS_ERROR("Memory allocation failed for key structure.");
        return NULL;
    }

    /**
     * Read all data from file.
     * MAXKEYS (secrets.h) is struct for key, _not_ length-related macro.
     */
    len = read(fd, keys, sizeof(MAXKEYS));

    if (len != sizeof(MAXKEYS))
    {
        int eno = errno;
        errno = 0;
        close(fd);
        free(keys);
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Read from secrets file "
                  "%s failed. Read %d, expected %d bytes. Error %d, %s.",
                  secret_file,
                  len,
                  (int)sizeof(MAXKEYS),
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        return NULL;
    }

    /* Close the file */
    if (close(fd) < 0)
    {
        int eno = errno;
        errno = 0;
        free(keys);
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Failed closing the "
                  "secrets file %s. Error %d, %s.",
                  secret_file,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        return NULL;
    }
    ss_dassert(keys != NULL);
    return keys;
}

/**
 * secrets_writeKeys
 *
 * This routine writes into a binary file the AES encryption key
 * and the AES Init Vector
 *
 * @param secret_file   The file with secret keys
 * @return 0 on success and 1 on failure
 */
int secrets_writeKeys(const char *path)
{
    int fd, randfd;
    unsigned int randval;
    MAXKEYS key;
    char secret_file[PATH_MAX + 10];

    if (strlen(path) > PATH_MAX)
    {
        MXS_ERROR("Pathname too long.");
        return 1;
    }

    snprintf(secret_file, PATH_MAX + 9, "%s/.secrets", path);
    secret_file[PATH_MAX + 9] = '\0';

    /* Open for writing | Create | Truncate the file for writing */
    if ((fd = open(secret_file, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR)) < 0)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("failed opening secret "
                  "file [%s]. Error %d, %s.",
                  secret_file,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        return 1;
    }

    /* Open for writing | Create | Truncate the file for writing */
    if ((randfd = open("/dev/random", O_RDONLY)) < 0)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("failed opening /dev/random. Error %d, %s.",
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        close(fd);
        return 1;
    }

    if (read(randfd, (void*) &randval, sizeof(unsigned int)) < 1)
    {
        MXS_ERROR("failed to read /dev/random.");
        close(fd);
        close(randfd);
        return 1;
    }

    close(randfd);
    secrets_random_str(key.enckey, MAXSCALE_KEYLEN);
    secrets_random_str(key.initvector, MAXSCALE_IV_LEN);

    /* Write data */
    if (write(fd, &key, sizeof(key)) < 0)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("failed writing into "
                  "secret file [%s]. Error %d, %s.",
                  secret_file,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        close(fd);
        return 1;
    }

    /* close file */
    if (close(fd) < 0)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("failed closing the "
                  "secret file [%s]. Error %d, %s.",
                  secret_file,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
    }

    if (chmod(secret_file, S_IRUSR) < 0)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("failed to change the permissions of the"
                  "secret file [%s]. Error %d, %s.",
                  secret_file,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
    }

    return 0;
}

/**
 * Decrypt a password that is stored inthe MaxScale configuration file.
 * If the password is not encrypted, ie is not a HEX string, then the
 * original is returned, this allows for backward compatibility with
 * unencrypted password.
 *
 * Note the return is always a malloc'd string that the caller must free
 *
 * @param crypt The encrypted password
 * @return  The decrypted password
 */
char *
decryptPassword(const char *crypt)
{
    MAXKEYS *keys;
    AES_KEY aeskey;
    unsigned char *plain;
    const char *ptr;
    unsigned char encrypted[80];
    int enlen;

    keys = secrets_readKeys(NULL);
    if (!keys)
    {
        return strdup(crypt);
    }
    /*
    ** If the input is not a HEX string return the input
    ** it probably was not encrypted
    */
    for (ptr = crypt; *ptr; ptr++)
    {
        if (!isxdigit(*ptr))
        {
            free(keys);
            return strdup(crypt);
        }
    }

    enlen = strlen(crypt) / 2;
    gw_hex2bin(encrypted, crypt, strlen(crypt));

    if ((plain = (unsigned char *) malloc(80)) == NULL)
    {
        free(keys);
        return NULL;
    }

    AES_set_decrypt_key(keys->enckey, 8 * MAXSCALE_KEYLEN, &aeskey);

    AES_cbc_encrypt(encrypted, plain, enlen, &aeskey, keys->initvector, AES_DECRYPT);
    free(keys);

    return(char *) plain;
}

/**
 * Encrypt a password that can be stored in the MaxScale configuration file.
 *
 * Note the return is always a malloc'd string that the caller must free
 * @param path      Path the the .secrets file
 * @param password  The password to encrypt
 * @return  The encrypted password
 */
char *
encryptPassword(const char* path, const char *password)
{
    MAXKEYS *keys;
    AES_KEY aeskey;
    int padded_len;
    char *hex_output;
    unsigned char padded_passwd[80];
    unsigned char encrypted[80];

    if ((keys = secrets_readKeys(path)) == NULL)
    {
        return NULL;
    }

    memset(padded_passwd, 0, 80);
    strncpy((char *) padded_passwd, password, 79);
    padded_len = ((strlen(password) / AES_BLOCK_SIZE) + 1) * AES_BLOCK_SIZE;

    AES_set_encrypt_key(keys->enckey, 8 * MAXSCALE_KEYLEN, &aeskey);

    AES_cbc_encrypt(padded_passwd, encrypted, padded_len, &aeskey, keys->initvector, AES_ENCRYPT);
    hex_output = (char *) malloc(padded_len * 2);
    gw_bin2hex(hex_output, encrypted, padded_len);
    free(keys);

    return hex_output;
}

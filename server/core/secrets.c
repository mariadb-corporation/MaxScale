/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/secrets.h>

#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

#include <openssl/aes.h>

#include <maxscale/alloc.h>
#include <maxscale/log_manager.h>
#include <maxscale/paths.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/random_jkiss.h>

#include "maxscale/secrets.h"

/**
 * Generate a random printable character
 *
 * @return A random printable character
 */
static unsigned char
secrets_randomchar()
{
    return (char) ((random_jkiss() % ('~' - ' ')) + ' ');
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
    static const char NAME[] = ".secrets";
    char secret_file[PATH_MAX + 1 + sizeof(NAME)]; // Worst case: maximum path + "/" + name.
    MAXKEYS *keys;
    struct stat secret_stats;
    static int reported = 0;

    if (path != NULL)
    {
        size_t len = strlen(path);
        if (len > PATH_MAX)
        {
            MXS_ERROR("Too long (%lu > %d) path provided.", len, PATH_MAX);
            return NULL;
        }

        if (stat(path, &secret_stats) == 0)
        {
            if (S_ISDIR(secret_stats.st_mode))
            {
                sprintf(secret_file, "%s/%s", path, NAME);
            }
            else
            {
                // If the provided path does not refer to a directory, then the
                // file name *must* be ".secrets".
                char *file;
                if ((file = strrchr(secret_file, '.')) == NULL || strcmp(file, NAME) != 0)
                {
                    MXS_ERROR("The name of the secrets file must be \"%s\".", NAME);
                    return NULL;
                }
            }
        }
        else
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_ERROR("The provided path \"%s\" does not exist or cannot be accessed. "
                      "Error: %d, %s.", path, errno, strerror_r(errno, errbuf, sizeof(errbuf)));
            return NULL;
        }

        clean_up_pathname(secret_file);
    }
    else
    {
        // We assume that get_datadir() returns a path shorter than PATH_MAX.
        sprintf(secret_file, "%s/%s", get_datadir(), NAME);
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
                char errbuf[MXS_STRERROR_BUFLEN];
                MXS_NOTICE("Encrypted password file %s can't be accessed "
                           "(%s). Password encryption is not used.",
                           secret_file,
                           strerror_r(eno, errbuf, sizeof(errbuf)));
                reported = 1;
            }
        }
        else
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_ERROR("Access for secrets file "
                      "[%s] failed. Error %d, %s.",
                      secret_file,
                      eno,
                      strerror_r(eno, errbuf, sizeof(errbuf)));
        }
        return NULL;
    }

    /* open secret file */
    int fd = open(secret_file, O_RDONLY);
    if (fd < 0)
    {
        int eno = errno;
        errno = 0;
        char errbuf[MXS_STRERROR_BUFLEN];
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
        char errbuf[MXS_STRERROR_BUFLEN];
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
        char errbuf[MXS_STRERROR_BUFLEN];
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
        MXS_ERROR("Ignoring secrets file %s, invalid permissions."
                  "The only permission on the file should be owner:read.",
                  secret_file);
        return NULL;
    }

    if ((keys = (MAXKEYS *) MXS_MALLOC(sizeof(MAXKEYS))) == NULL)
    {
        close(fd);
        return NULL;
    }

    /**
     * Read all data from file.
     * MAXKEYS (secrets.h) is struct for key, _not_ length-related macro.
     */
    ssize_t len = read(fd, keys, sizeof(MAXKEYS));

    if (len != sizeof(MAXKEYS))
    {
        int eno = errno;
        errno = 0;
        close(fd);
        MXS_FREE(keys);
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Read from secrets file "
                  "%s failed. Read %ld, expected %d bytes. Error %d, %s.",
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
        MXS_FREE(keys);
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed closing the "
                  "secrets file %s. Error %d, %s.",
                  secret_file,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        return NULL;
    }
    ss_dassert(keys != NULL);

    /** Successfully loaded keys, log notification */
    if (!reported)
    {
        MXS_NOTICE("Using encrypted passwords. Encryption key: '%s'.", secret_file);
        reported = 1;
    }

    return keys;
}

/**
 * secrets_writeKeys
 *
 * This routine writes into a binary file the AES encryption key
 * and the AES Init Vector
 *
 * @param dir The directory where the ".secrets" file should be created.
 * @return 0 on success and 1 on failure
 */
int secrets_write_keys(const char *dir)
{
    int fd, randfd;
    unsigned int randval;
    MAXKEYS key;
    char secret_file[PATH_MAX + 10];

    if (strlen(dir) > PATH_MAX)
    {
        MXS_ERROR("Pathname too long.");
        return 1;
    }

    snprintf(secret_file, PATH_MAX + 9, "%s/.secrets", dir);
    clean_up_pathname(secret_file);

    /* Open for writing | Create | Truncate the file for writing */
    if ((fd = open(secret_file, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR)) < 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
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
        char errbuf[MXS_STRERROR_BUFLEN];
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
        char errbuf[MXS_STRERROR_BUFLEN];
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
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("failed closing the "
                  "secret file [%s]. Error %d, %s.",
                  secret_file,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
    }

    if (chmod(secret_file, S_IRUSR) < 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
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
 * @return  The decrypted password or NULL if allocation failure.
 */
char *
decrypt_password(const char *crypt)
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
        return MXS_STRDUP(crypt);
    }
    /*
    ** If the input is not a HEX string return the input
    ** it probably was not encrypted
    */
    for (ptr = crypt; *ptr; ptr++)
    {
        if (!isxdigit(*ptr))
        {
            MXS_FREE(keys);
            return MXS_STRDUP(crypt);
        }
    }

    enlen = strlen(crypt) / 2;
    gw_hex2bin(encrypted, crypt, strlen(crypt));

    if ((plain = (unsigned char *) MXS_MALLOC(enlen + 1)) == NULL)
    {
        MXS_FREE(keys);
        return NULL;
    }

    AES_set_decrypt_key(keys->enckey, 8 * MAXSCALE_KEYLEN, &aeskey);

    AES_cbc_encrypt(encrypted, plain, enlen, &aeskey, keys->initvector, AES_DECRYPT);
    plain[enlen] = '\0';
    MXS_FREE(keys);

    return (char *) plain;
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
encrypt_password(const char* path, const char *password)
{
    MAXKEYS *keys;
    AES_KEY aeskey;
    int padded_len;
    char *hex_output;
    unsigned char padded_passwd[MXS_PASSWORD_MAXLEN + 1];
    unsigned char encrypted[MXS_PASSWORD_MAXLEN + 1];

    if ((keys = secrets_readKeys(path)) == NULL)
    {
        return NULL;
    }

    memset(padded_passwd, 0, MXS_PASSWORD_MAXLEN + 1);
    strncpy((char *) padded_passwd, password, MXS_PASSWORD_MAXLEN);
    padded_len = ((strlen((char*)padded_passwd) / AES_BLOCK_SIZE) + 1) * AES_BLOCK_SIZE;

    AES_set_encrypt_key(keys->enckey, 8 * MAXSCALE_KEYLEN, &aeskey);

    AES_cbc_encrypt(padded_passwd, encrypted, padded_len, &aeskey, keys->initvector, AES_ENCRYPT);
    hex_output = (char *) MXS_MALLOC(padded_len * 2 + 1);
    if (hex_output)
    {
        gw_bin2hex(hex_output, encrypted, padded_len);
    }
    MXS_FREE(keys);

    return hex_output;
}

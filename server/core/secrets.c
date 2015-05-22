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

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;
/**
 * Generate a random printable character
 *
 * @return A random printable character
 */
static unsigned char
secrets_randomchar()
{
	return (char)((rand() % ('~' - ' ')) + ' ');
}

static int
secrets_random_str(unsigned char *output, int len)
{
int i;
	srand((unsigned long )time(0L) ^ (unsigned long )output);

	for ( i = 0; i < len; ++i )
	{
                output[i] = secrets_randomchar();
        }
        return 0;
}

/**
 * secrets_readKeys
 *
 * This routine reads data from a binary file and extracts the AES encryption key
 * and the AES Init Vector
 *
 * @return	The keys structure or NULL on error
 */
static MAXKEYS *
secrets_readKeys()
{
char		secret_file[PATH_MAX+1];
char		*home;
MAXKEYS		*keys;
struct stat 	secret_stats;
int		fd;
int             len;
static int	reported = 0;

	snprintf(secret_file, PATH_MAX, "%s/.secrets", get_datadir());

	/* Try to access secrets file */
	if (access(secret_file, R_OK) == -1) 
	{
                int eno = errno;
                errno = 0;
		if (eno == ENOENT)
		{
			if (!reported)
			{
				LOGIF(LM, (skygw_log_write(
				LOGFILE_MESSAGE,
				"Encrypted password file %s can't be accessed "
				"(%s). Password encryption is not used.",
				secret_file,
				strerror(eno))));
				reported = 1;
			}
		}
		else
		{
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Error : access for secrets file "
				"[%s] failed. Error %d, %s.",
				secret_file,
				eno,
				strerror(eno))));
		}
		return NULL;
        }

	/* open secret file */
	if ((fd = open(secret_file, O_RDONLY)) < 0)
	{
                int eno = errno;
                errno = 0;
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed opening secret "
                        "file [%s]. Error %d, %s.",
                        secret_file,
                        eno,
                        strerror(eno))));
		return NULL;

	}

	/* accessing file details */
	if (fstat(fd, &secret_stats) < 0) {
                int eno = errno;
                errno = 0;
		close(fd);
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : fstat for secret file %s "
                        "failed. Error %d, %s.",
                        secret_file,
                        eno,
                        strerror(eno))));
		return NULL;	
	}	

	if (secret_stats.st_size != sizeof(MAXKEYS))
	{
                int eno = errno;
                errno = 0;
		close(fd);
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Secrets file %s has "
                        "incorrect size. Error %d, %s.",
                        secret_file,
                        eno,
                        strerror(eno))));
		return NULL;
	}
	if (secret_stats.st_mode != (S_IRUSR|S_IFREG))
	{
		close(fd);
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Ignoring secrets file "
                        "%s, invalid permissions.",
                        secret_file)));
		return NULL;
	}

	if ((keys = (MAXKEYS *)malloc(sizeof(MAXKEYS))) == NULL)
	{
		close(fd);
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Memory allocation failed "
                        "for key structure.")));
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
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Read from secrets file "
                        "%s failed. Read %d, expected %d bytes. Error %d, %s.",
                        secret_file,
                        len,
                        sizeof(MAXKEYS),
                        eno,
                        strerror(eno))));
		return NULL;
	}

	/* Close the file */
	if (close(fd) < 0) {
                int eno = errno;
                errno = 0;
		free(keys);
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed closing the "
                        "secrets file %s. Error %d, %s.",
                        secret_file,
                        eno,
                        strerror(eno))));
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
int secrets_writeKeys(char *secret_file)
{
int				fd,randfd;
unsigned int	randval;
MAXKEYS			key;

	/* Open for writing | Create | Truncate the file for writing */
        if ((fd = open(secret_file, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR)) < 0)
	{
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : failed opening secret "
                        "file [%s]. Error %d, %s.",
                        secret_file,
                        errno,
                        strerror(errno))));
		return 1;
	}

	/* Open for writing | Create | Truncate the file for writing */
        if ((randfd = open("/dev/random", O_RDONLY)) < 0)
	{
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : failed opening /dev/random. Error %d, %s.",
                        errno,
                        strerror(errno))));
		close(fd);
		return 1;
	}

		if(read(randfd,(void*)&randval,sizeof(unsigned int)) < 1)
    {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
						"Error : failed to read /dev/random.")));
		close(fd);
		close(randfd);
		return 1;
    }

    close(randfd);
	srand(randval);
	secrets_random_str(key.enckey, MAXSCALE_KEYLEN);
	secrets_random_str(key.initvector, MAXSCALE_IV_LEN);

	/* Write data */
	if (write(fd, &key, sizeof(key)) < 0)
	{
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : failed writing into "
                        "secret file [%s]. Error %d, %s.",
                        secret_file,
                        errno,
                        strerror(errno))));
		close(fd);
		return 1;
	}

	/* close file */
	if (close(fd) < 0)
	{
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : failed closing the "
                        "secret file [%s]. Error %d, %s.",
                        secret_file,
                        errno,
                        strerror(errno))));
	}

	if( chmod(secret_file, S_IRUSR) < 0)
	{
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : failed to change the permissions of the"
                        "secret file [%s]. Error %d, %s.",
                        secret_file,
                        errno,
                        strerror(errno))));
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
 * @param crypt	The encrypted password
 * @return	The decrypted password
 */
char *
decryptPassword(char *crypt)
{
MAXKEYS		*keys;
AES_KEY		aeskey;
unsigned char	*plain;
char		*ptr;
unsigned char	encrypted[80];
int		enlen;

	keys = secrets_readKeys();
	if (!keys)
		return strdup(crypt);
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

	if ((plain = (unsigned char *)malloc(80)) == NULL)
	{
		free(keys);
		return NULL;
	}

	AES_set_decrypt_key(keys->enckey, 8 * MAXSCALE_KEYLEN, &aeskey);

	AES_cbc_encrypt(encrypted, plain, enlen, &aeskey, keys->initvector, AES_DECRYPT);
	free(keys);

	return (char *)plain;
}

/**
 * Encrypt a password that can be stored in the MaxScale configuration file.
 *
 * Note the return is always a malloc'd string that the caller must free
 *
 * @param password	The password to encrypt
 * @return	The encrypted password
 */
char *
encryptPassword(char *password)
{
MAXKEYS		*keys;
AES_KEY		aeskey;
int		padded_len;
char		*hex_output;
unsigned char	padded_passwd[80];
unsigned char	encrypted[80];

	if ((keys = secrets_readKeys()) == NULL)
		return NULL;

	memset(padded_passwd, 0, 80);
	strncpy((char *)padded_passwd, password, 79);
	padded_len = ((strlen(password) / AES_BLOCK_SIZE) + 1) * AES_BLOCK_SIZE;

	AES_set_encrypt_key(keys->enckey, 8 * MAXSCALE_KEYLEN, &aeskey);

	AES_cbc_encrypt(padded_passwd, encrypted, padded_len, &aeskey, keys->initvector, AES_ENCRYPT);
	hex_output = (char *)malloc(padded_len * 2);
	gw_bin2hex(hex_output, encrypted, padded_len);
	free(keys);

	return	hex_output;
}

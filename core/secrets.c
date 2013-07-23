/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
 */


#include "secrets.h"

static char secrets_randomchar() {
   return (char)((rand() % 78) + 30);
}

static int secrets_random_str(char *output, int len) {
        int i;
        srand(time(0L));

        for ( i = 0; i < len; ++i ) {
                output[i] = secrets_randomchar();
        }

        output[len]='\0';

        return 0;
}

/**
 * secrets_readKeys
 *
 * This routine reads data from a binary file and exracts the AES encryption key
 * and the AES Init Vector
 *
 * Input parameters must be preallocated
 * @param enc_key	Will contain the encryption key found in file
 * @param iv		Will contain the Init vector found in file
 * @param secret_file	The file with secret keys
 * @return 0 on success and 1 on failure
 */

int secrets_readKeys(char *enc_key, char *iv, char *secret_file) {   
	struct stat secret_stats;
	char read_buffer[1 + AES_BLOCK_SIZE * 2 + AES_BLOCK_SIZE + 3]="";
	int fd =0;
	int secret_file_size = 0;

	/* open secret file */
	fd = open(secret_file, O_RDONLY);

	if (fd < 0) {
		fprintf(stderr, "secrets_readKeys, failed opening secret file [%s]. Error %i, %s\n", secret_file, errno, strerror(errno));
		return 1;

	}

	/* accessing file details */
	if (fstat(fd, &secret_stats) < 0) {
		fprintf(stderr, "secrets_readKeys, failed accessing secret file details [%s]. Error %i, %s\n", secret_file, errno, strerror(errno));
		return 1;	
	}	

	secret_file_size = secret_stats.st_size;

	fprintf(stderr, "The secret file has %i bytes\n", secret_file_size);

	/* read all data from file */
	if (read(fd, read_buffer, sizeof(read_buffer)-1) < 0) {
		fprintf(stderr, "secrets_readKeys, failed reading from  secret file [%s]. Error %i, %s\n", secret_file, errno, strerror(errno));
		return 1;
	}

	/* Now filling input parameters */
	memcpy(enc_key, read_buffer+1, MAXSCALE_SECRETS_ONE);
	memcpy(iv, read_buffer+1+MAXSCALE_SECRETS_ONE, MAXSCALE_SECRETS_INIT_VAL_ONE);
	memcpy(enc_key+ MAXSCALE_SECRETS_ONE, read_buffer+1+MAXSCALE_SECRETS_ONE+MAXSCALE_SECRETS_INIT_VAL_ONE, MAXSCALE_SECRETS_TWO);
	memcpy(iv+MAXSCALE_SECRETS_INIT_VAL_ONE, read_buffer+1+MAXSCALE_SECRETS_ONE+MAXSCALE_SECRETS_INIT_VAL_ONE+MAXSCALE_SECRETS_TWO, MAXSCALE_SECRETS_INIT_VAL_TWO);

	/* Close the file */
	if (close(fd) < 0) {
		fprintf(stderr, "secrets_readKeys, failed closing the secret file [%s]. Error %i, %s\n", secret_file, errno, strerror(errno));
		return 1;
	}

	return 0;
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
	char enc_key[1 + AES_BLOCK_SIZE * 2]="";
	char iv[1 + AES_BLOCK_SIZE]="";
	char secret_buffer[1 + AES_BLOCK_SIZE * 3 + 3] = "";
	char scramble_secret[1 + AES_BLOCK_SIZE * 3 + 3] = "";

	char one_byte[1 + 1]="";
	char two_bytes[1 + 2]="";

	int fd =0;

	/* Open for writing | Create | Truncate the file for writing */
	fd = open(secret_file, O_CREAT | O_WRONLY | O_TRUNC);

	if (fd < 0) {
		fprintf(stderr, "secrets_createKeys, failed opening secret file [%s]. Error %i, %s\n", secret_file, errno, strerror(errno));
		return 1;
	}

	srand(time(NULL));
	secrets_random_str(secret_buffer, AES_BLOCK_SIZE * 3 + 3);

	/* assign key and iv from random buffer */	
	memcpy(one_byte, secret_buffer, 1);
	memcpy(enc_key, secret_buffer + 1, AES_BLOCK_SIZE * 2);
	memcpy(iv, secret_buffer + 1 + AES_BLOCK_SIZE * 2, AES_BLOCK_SIZE);
	memcpy(two_bytes, secret_buffer + 1 + AES_BLOCK_SIZE * 2 + AES_BLOCK_SIZE + 1, 2);

	//fprintf(stderr, "<<< Key32 is [%s]\n", enc_key);
	//fprintf(stderr, "<<< IV16 is [%s]\n", iv);

	/* prepare data */
	memcpy(scramble_secret, one_byte, 1);
	memcpy(scramble_secret + 1, enc_key, MAXSCALE_SECRETS_ONE);
	memcpy(scramble_secret + 1 + MAXSCALE_SECRETS_ONE, iv, MAXSCALE_SECRETS_INIT_VAL_ONE);
	memcpy(scramble_secret + 1 + MAXSCALE_SECRETS_ONE + MAXSCALE_SECRETS_INIT_VAL_ONE, enc_key + MAXSCALE_SECRETS_ONE, MAXSCALE_SECRETS_TWO);
	memcpy(scramble_secret + 1 + MAXSCALE_SECRETS_ONE + MAXSCALE_SECRETS_INIT_VAL_ONE + MAXSCALE_SECRETS_TWO, iv + MAXSCALE_SECRETS_INIT_VAL_ONE, MAXSCALE_SECRETS_INIT_VAL_TWO);
	memcpy(scramble_secret + 1 + MAXSCALE_SECRETS_ONE + MAXSCALE_SECRETS_INIT_VAL_ONE + MAXSCALE_SECRETS_TWO + MAXSCALE_SECRETS_INIT_VAL_TWO, two_bytes, 2);

	/* Write data */
	if(write(fd, scramble_secret, sizeof(scramble_secret)-1) < 0) {
		fprintf(stderr, "secrets_createKeys, failed writing into secret file [%s]. Error %i, %s\n", secret_file, errno, strerror(errno));
		return 1;
	}

	/* close file */
	if (close(fd) < 0) {
		fprintf(stderr, "secrets_createKeys, failed closing the secret file [%s]. Error %i, %s\n", secret_file, errno, strerror(errno));
	}

	return 0;
}

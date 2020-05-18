/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-04-23
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/secrets.hh>

#include <cctype>
#include <fcntl.h>
#include <openssl/aes.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <maxbase/alloc.h>
#include <maxscale/paths.hh>
#include <maxscale/random.h>
#include <maxscale/utils.hh>
#include <maxbase/format.hh>

#include "internal/secrets.hh"

using std::string;

namespace
{

struct ThisUnit
{
    std::unique_ptr<EncryptionKeys> keys;
};
ThisUnit this_unit;

/**
 * Generate a random printable character
 *
 * @return A random printable character
 */
unsigned char secrets_randomchar()
{
    return (char)((mxs_random() % ('~' - ' ')) + ' ');
}

int secrets_random_str(unsigned char* output, int len)
{
    for (int i = 0; i < len; ++i)
    {
        output[i] = secrets_randomchar();
    }
    return 0;
}
}

/**
 * This routine reads data from a binary file named ".secrets" and extracts the AES encryption key
 * and the AES Init Vector.
 * If the path parameter is not null the custom path is interpreted as a folder
 * containing the .secrets file. Otherwise the default location is used.
 * @return  The keys structure or NULL on error
 */
std::unique_ptr<EncryptionKeys> secrets_readKeys(const char* path)
{
    const char NAME[] = ".secrets";
    string secret_file;
    struct stat secret_stats { 0 };

    if (path != nullptr)
    {
        size_t len = strlen(path);
        if (len > PATH_MAX)
        {
            MXS_ERROR("Too long (%lu > %d) path provided.", len, PATH_MAX);
            return nullptr;
        }

        if (stat(path, &secret_stats) == 0)
        {
            if (S_ISDIR(secret_stats.st_mode))
            {
                secret_file = mxb::string_printf("%s/%s", path, NAME);
            }
            else
            {
                // If the provided path does not refer to a directory, then the
                // file name *must* be ".secrets".
                const char* file = strrchr(secret_file.c_str(), '.');
                if (file == nullptr || strcmp(file, NAME) != 0)
                {
                    MXS_ERROR("The name of the secrets file must be '%s'.", NAME);
                    return nullptr;
                }
            }
        }
        else
        {
            MXS_ERROR("The provided path '%s' does not exist or cannot be accessed. Error: %d, %s.",
                      path, errno, mxs_strerror(errno));
            return nullptr;
        }

        secret_file = clean_up_pathname(secret_file);
    }
    else
    {
        // We assume that mxs::datadir() returns a path shorter than PATH_MAX.
        secret_file = mxb::string_printf("%s/%s", mxs::datadir(), NAME);
    }

    static int reported = 0;
    /* Try to access secrets file */
    const char* secret_filez = secret_file.c_str();
    if (access(secret_filez, R_OK) == -1)
    {
        int eno = errno;
        errno = 0;
        if (eno == ENOENT)
        {
            if (!reported)
            {
                MXS_NOTICE("Encrypted password file %s can't be accessed (%s). Password encryption is "
                           "not used.", secret_filez, mxs_strerror(eno));
                reported = 1;
            }
        }
        else
        {
            MXS_ERROR("Access for secrets file [%s] failed. Error %d, %s.",
                      secret_filez, eno, mxs_strerror(eno));
        }
        return nullptr;
    }

    /* open secret file */
    int fd = open(secret_filez, O_RDONLY);
    if (fd < 0)
    {
        int eno = errno;
        errno = 0;
        MXS_ERROR("Failed opening secret file [%s]. Error %d, %s.", secret_filez, eno, mxs_strerror(eno));
        return nullptr;
    }

    /* accessing file details */
    if (fstat(fd, &secret_stats) < 0)
    {
        int eno = errno;
        errno = 0;
        close(fd);
        MXS_ERROR("fstat for secret file %s failed. Error %d, %s.", secret_filez, eno, mxs_strerror(eno));
        return nullptr;
    }

    if (secret_stats.st_size != sizeof(EncryptionKeys))
    {
        int eno = errno;
        errno = 0;
        close(fd);
        MXS_ERROR("Secrets file %s has incorrect size. Error %d, %s.", secret_filez, eno, mxs_strerror(eno));
        return nullptr;
    }

    if (secret_stats.st_mode != (S_IRUSR | S_IFREG))
    {
        close(fd);
        MXS_ERROR("Ignoring secrets file %s, invalid permissions. The only permission on the file should be "
                  "owner:read.", secret_filez);
        return nullptr;
    }

    auto keys = std::make_unique<EncryptionKeys>();
    if (!keys)
    {
        close(fd);
        return nullptr;
    }

    /**
     * Read all data from file.
     * EncryptionKeys (secrets.h) is struct for key, _not_ length-related macro.
     */
    ssize_t len = read(fd, keys.get(), sizeof(EncryptionKeys));

    if (len != sizeof(EncryptionKeys))
    {
        int eno = errno;
        errno = 0;
        close(fd);
        MXS_ERROR("Read from secrets file %s failed. Read %ld, expected %d bytes. Error %d, %s.",
                  secret_filez, len, (int)sizeof(EncryptionKeys), eno, mxs_strerror(eno));
        return nullptr;
    }

    /* Close the file */
    if (close(fd) < 0)
    {
        int eno = errno;
        errno = 0;
        MXS_ERROR("Failed closing the secrets file %s. Error %d, %s.",
                  secret_filez, eno, mxs_strerror(eno));
        return NULL;
    }

    /** Successfully loaded keys, log notification */
    if (!reported)
    {
        MXS_NOTICE("Using encrypted passwords. Encryption key: '%s'.", secret_filez);
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
int secrets_write_keys(const string& dir)
{
    if (dir.length() > PATH_MAX)
    {
        MXS_ERROR("Pathname too long.");
        return 1;
    }

    string secret_file = dir + "/.secrets";
    secret_file = clean_up_pathname(secret_file);
    auto secret_filez = secret_file.c_str();

    /* Open for writing | Create | Truncate the file for writing */
    int fd = open(secret_filez, O_EXCL | O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR);
    if (fd < 0)
    {
        if (errno == EEXIST)
        {
            fprintf(stderr, "Key [%s] already exists, remove it manually to create a new one. \n",
                    secret_filez);
        }

        MXS_ERROR("failed opening secret file [%s]. Error %d, %s.", secret_filez, errno, mxs_strerror(errno));
        return 1;
    }

    EncryptionKeys key;
    secrets_random_str(key.enckey, EncryptionKeys::key_len);
    secrets_random_str(key.initvector, EncryptionKeys::iv_len);

    /* Write data */
    if (write(fd, &key, sizeof(key)) < 0)
    {
        MXS_ERROR("failed writing into secret file [%s]. Error %d, %s.",
                  secret_filez, errno, mxs_strerror(errno));
        close(fd);
        return 1;
    }

    /* close file */
    if (close(fd) < 0)
    {
        MXS_ERROR("failed closing the secret file [%s]. Error %d, %s.",
                  secret_filez, errno, mxs_strerror(errno));
    }

    if (chmod(secret_filez, S_IRUSR) < 0)
    {
        MXS_ERROR("failed to change the permissions of the secret file [%s]. Error %d, %s.",
                  secret_filez, errno, mxs_strerror(errno));
    }

    return 0;
}

/**
 * Decrypt a password that is stored in the MaxScale configuration file.
 * If the password is not encrypted, ie is not a HEX string, then the
 * original is returned, this allows for backward compatibility with
 * unencrypted password.
 *
 * @param crypt The encrypted password
 * @return The decrypted password
 */
string decrypt_password(const string& crypt)
{
    const auto* keys = this_unit.keys.get();
    if (!keys)
    {
        // Password encryption is not used, so return original.
        return crypt;
    }

    // If the input is not a HEX string, return the input as is. Likely it was not encrypted.
    for (auto c : crypt)
    {
        if (!isxdigit(c))
        {
            return crypt;
        }
    }

    // Convert to binary.
    size_t hex_len = crypt.length();
    auto bin_len = hex_len / 2;
    unsigned char encrypted_bin[bin_len];
    mxs::hex2bin(crypt.c_str(), hex_len, encrypted_bin);

    AES_KEY aeskey;
    AES_set_decrypt_key(keys->enckey, 8 * EncryptionKeys::key_len, &aeskey);

    auto plain_len = bin_len + 1;   // Decryption output cannot be longer than input data.
    unsigned char plain[plain_len];
    memset(plain, '\0', plain_len);

    // Need to copy the init vector as it's modified during decryption.
    unsigned char init_vector[EncryptionKeys::iv_len];
    memcpy(init_vector, keys->initvector, EncryptionKeys::iv_len);
    AES_cbc_encrypt(encrypted_bin, plain, bin_len, &aeskey, init_vector, AES_DECRYPT);

    string rval((char*)plain);
    return rval;
}

/**
 * Encrypt a password that can be stored in the MaxScale configuration file.
 *
 * @param path   Path to the the .secrets file
 * @param input  The plaintext password to encrypt.
 * @return The encrypted password, or empty on failure.
 */
string encrypt_password(const char* path, const char* input)
{
    auto keys = secrets_readKeys(path);
    if (!keys)
    {
        return "";
    }

    AES_KEY aeskey;
    AES_set_encrypt_key(keys->enckey, 8 * EncryptionKeys::key_len, &aeskey);

    size_t len = strlen(input);
    size_t padded_len = ((len / AES_BLOCK_SIZE) + 1) * AES_BLOCK_SIZE;
    unsigned char encrypted[padded_len + 1];

    AES_cbc_encrypt((const unsigned char*) input, encrypted, padded_len,
                    &aeskey, keys->initvector, AES_ENCRYPT);

    char hex_output[2 * padded_len + 1];
    mxs::bin2hex(encrypted, padded_len, hex_output);
    return hex_output;
}

bool load_encryption_keys()
{
    auto keys = secrets_readKeys(nullptr);
    if (keys)
    {
        mxb_assert(this_unit.keys == nullptr);
        this_unit.keys = move(keys);
    }
    // TODO: return error if failed unexpectedly.
    return true;
}

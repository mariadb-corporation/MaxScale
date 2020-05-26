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
#include <fstream>
#include <sys/stat.h>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ossl_typ.h>

#include <maxbase/format.hh>
#include <maxscale/paths.hh>
#include <maxscale/utils.hh>
#include "internal/secrets.hh"

using std::string;

const char* const SECRETS_FILENAME = ".secrets";

namespace
{

struct ThisUnit
{
    std::unique_ptr<EncryptionKeys> keys;
};
ThisUnit this_unit;

enum class ProcessingMode
{
    ENCRYPT,
    DECRYPT,
    DECRYPT_IGNORE_ERRORS
};
void print_openSSL_errors(const char* operation);

/**
 * Encrypt or decrypt the input buffer to output buffer.
 *
 * @param key Encryption key
 * @param mode Encrypting or decrypting
 * @param input Input buffer
 * @param input_len Input length
 * @param output Output buffer
 * @param output_len Produced output length is written here
 * @return True on success
 */
bool encrypt_or_decrypt(const EncryptionKeys& key, ProcessingMode mode,
                        const uint8_t* input, int input_len, uint8_t* output, int* output_len)
{
    auto ctx = EVP_CIPHER_CTX_new();
    int enc = (mode == ProcessingMode::ENCRYPT) ? AES_ENCRYPT : AES_DECRYPT;
    bool ignore_errors = (mode == ProcessingMode::DECRYPT_IGNORE_ERRORS);
    bool ok = false;

    if (EVP_CipherInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.enckey, key.initvector,
                          enc) == 1 || ignore_errors)
    {
        int output_written = 0;
        if (EVP_CipherUpdate(ctx, output, &output_written, input, input_len) == 1 || ignore_errors)
        {
            int total_output_len = output_written;
            if (EVP_CipherFinal_ex(ctx, output + total_output_len, &output_written) == 1 || ignore_errors)
            {
                total_output_len += output_written;
                *output_len = total_output_len;
                ok = true;
            }
        }
    }

    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
    {
        const char* operation = (mode == ProcessingMode::ENCRYPT) ? "when encrypting password" :
            "when decrypting password";
        print_openSSL_errors(operation);
    }
    return ok;
}

void print_openSSL_errors(const char* operation)
{
    // It's unclear how thread(unsafe) OpenSSL error functions are. Minimize such possibilities by
    // using a local buffer.
    constexpr size_t bufsize = 256;     // Should be enough according to some googling.
    char buf[bufsize];
    buf[0] = '\0';

    auto errornum = ERR_get_error();
    auto errornum2 = ERR_get_error();
    ERR_error_string_n(errornum, buf, bufsize);

    if (errornum2 == 0)
    {
        // One error.
        MXB_ERROR("OpenSSL error %s. %s", operation, buf);
    }
    else
    {
        // Multiple errors, print all as separate messages.
        MXB_ERROR("Multiple OpenSSL errors %s. Detailed messages below.", operation);
        MXB_ERROR("%s", buf);
        while (errornum2 != 0)
        {
            ERR_error_string_n(errornum2, buf, bufsize);
            MXB_ERROR("%s", buf);
            errornum2 = ERR_get_error();
        }
    }
}
}

/**
 * Reads binary data from file and extracts the AES encryption key and init vector. The source file needs to
 * be a certain size and have expected permissions. If the source file does not exist, returns empty results.
 *
 * @param filepath Path to binary file.
 * @return Result structure. Ok if file was loaded successfully or if file did not exist. False on error.
 */

ReadKeyResult secrets_readkeys(const string& filepath)
{
    ReadKeyResult rval;
    auto filepathc = filepath.c_str();

    // Before opening the file, check its size and permissions.
    struct stat filestats { 0 };
    bool stat_error = false;
    errno = 0;
    if (stat(filepathc, &filestats) == 0)
    {
        auto filesize = filestats.st_size;
        if (filesize != EncryptionKeys::total_len)
        {
            MXS_ERROR("Size of secrets file '%s' is %li when %i was expected.",
                      filepathc, filesize, EncryptionKeys::total_len);
            stat_error = true;
        }

        auto filemode = filestats.st_mode;
        if (!S_ISREG(filemode))
        {
            MXS_ERROR("Secrets file '%s' is not a regular file.", filepathc);
            stat_error = true;
        }
        else if ((filemode & (S_IRWXU | S_IRWXG | S_IRWXO)) != S_IRUSR)
        {
            MXS_ERROR("Secrets file '%s' permissions are wrong. The only permission on the file should be "
                      "owner:read.", filepathc);
            stat_error = true;
        }
    }
    else if (errno == ENOENT)
    {
        // The file does not exist. This is ok. Return empty result.
        rval.ok = true;
        return rval;
    }
    else
    {
        MXS_ERROR("stat() for secrets file '%s' failed. Error %d, %s.",
                  filepathc, errno, mxs_strerror(errno));
        stat_error = true;
    }

    if (stat_error)
    {
        return rval;
    }

    // Open file in binary read mode.
    errno = 0;
    std::ifstream file(filepath, std::ios_base::binary);
    if (file.is_open())
    {
        // Read all data from file.
        char readbuf[EncryptionKeys::total_len];
        file.read(readbuf, sizeof(readbuf));
        if (file.good())
        {
            // Success, copy contents to key structure.
            rval.key = std::make_unique<EncryptionKeys>();
            memcpy(rval.key->enckey, readbuf, EncryptionKeys::key_len);
            memcpy(rval.key->initvector, readbuf + EncryptionKeys::key_len, EncryptionKeys::iv_len);
            rval.ok = true;
        }
        else
        {
            MXS_ERROR("Read from secrets file %s failed. Read %li, expected %i bytes. Error %d, %s.",
                      filepathc, file.gcount(), EncryptionKeys::total_len, errno, mxs_strerror(errno));
        }
    }
    else
    {
        MXS_ERROR("Could not open secrets file '%s'. Error %d, %s.", filepathc, errno, mxs_strerror(errno));
    }
    return rval;
}

namespace maxscale
{
string decrypt_password(const string& crypt)
{
    const auto* key = this_unit.keys.get();
    if (!key)
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

    return decrypt_password(*key, crypt);
}
}

std::string decrypt_password(const EncryptionKeys& key, const std::string& input)
{
    string rval;
    // Convert to binary.
    size_t hex_len = input.length();
    auto bin_len = hex_len / 2;
    unsigned char encrypted_bin[bin_len];
    mxs::hex2bin(input.c_str(), hex_len, encrypted_bin);

    unsigned char plain[bin_len];   // Decryption output cannot be longer than input data.
    int decrypted_len = 0;
    // TODO: detect old encryption key and use correct decryption mode
    if (encrypt_or_decrypt(key, ProcessingMode::DECRYPT_IGNORE_ERRORS, encrypted_bin, bin_len, plain,
                           &decrypted_len))
    {
        // Decrypted data should be text.
        auto output_data = reinterpret_cast<const char*>(plain);
        rval.assign(output_data, decrypted_len);
    }
    return rval;
}

/**
 * Encrypt a password that can be stored in the MaxScale configuration file.
 *
 * @param key Encryption key and init vector
 * @param input The plaintext password to encrypt.
 * @return The encrypted password, or empty on failure.
 */
string encrypt_password(const EncryptionKeys& key, const string& input)
{
    string rval;
    // Output can be a block length longer than input.
    auto input_len = input.length();
    unsigned char encrypted_bin[input_len + AES_BLOCK_SIZE];

    // Although input is text, interpret as binary.
    auto input_data = reinterpret_cast<const uint8_t*>(input.c_str());
    int encrypted_len = 0;
    if (encrypt_or_decrypt(key, ProcessingMode::ENCRYPT,
                           input_data, input_len, encrypted_bin, &encrypted_len))
    {
        int hex_len = 2 * encrypted_len;
        char hex_output[hex_len + 1];
        mxs::bin2hex(encrypted_bin, encrypted_len, hex_output);
        rval.assign(hex_output, hex_len);
    }
    return rval;
}

bool load_encryption_keys()
{
    mxb_assert(this_unit.keys == nullptr);
    string path(mxs::datadir());
    path.append("/").append(SECRETS_FILENAME);
    auto ret = secrets_readkeys(path);
    if (ret.ok)
    {
        if (ret.key)
        {
            MXB_NOTICE("Using encrypted passwords. Encryption key read from '%s'.", path.c_str());
            this_unit.keys = move(ret.key);
        }
        else
        {
            MXB_NOTICE("Password encryption key file '%s' not found, using configured passwords as "
                       "plaintext.", path.c_str());
        }
    }
    return ret.ok;
}

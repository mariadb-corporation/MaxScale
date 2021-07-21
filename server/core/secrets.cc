/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/secrets.hh>

#include <cctype>
#include <fstream>
#include <pwd.h>
#include <sys/stat.h>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ossl_typ.h>
#include <openssl/rand.h>

#include <maxbase/format.hh>
#include <maxscale/paths.hh>
#include <maxscale/utils.hh>
#include <maxscale/version.h>
#include "internal/secrets.hh"

using std::string;

const char* const SECRETS_FILENAME = ".secrets";

namespace
{

struct ThisUnit
{
    ByteVec key;    /**< Password decryption key, assigned at startup */
    ByteVec iv;     /**< Decryption init vector, assigned at startup. Only used with old-format keys */
};
ThisUnit this_unit;

enum class ProcessingMode
{
    ENCRYPT,
    DECRYPT,
    DECRYPT_IGNORE_ERRORS
};

const char field_desc[] = "description";
const char field_version[] = "maxscale_version";
const char field_cipher[] = "encryption_cipher";
const char field_key[] = "encryption_key";
const char desc[] = "MaxScale encryption/decryption key";

#define SECRETS_CIPHER EVP_aes_256_cbc
#define STRINGIFY(X)  #X
#define STRINGIFY2(X) STRINGIFY(X)
const char CIPHER_NAME[] = STRINGIFY2(SECRETS_CIPHER);

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
bool encrypt_or_decrypt(const uint8_t* key, const uint8_t* iv, ProcessingMode mode,
                        const uint8_t* input, int input_len, uint8_t* output, int* output_len)
{
    auto ctx = EVP_CIPHER_CTX_new();
    int enc = (mode == ProcessingMode::ENCRYPT) ? AES_ENCRYPT : AES_DECRYPT;
    bool ignore_errors = (mode == ProcessingMode::DECRYPT_IGNORE_ERRORS);
    bool ok = false;

    if (EVP_CipherInit_ex(ctx, secrets_cipher(), nullptr, key, iv, enc) == 1 || ignore_errors)
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

const EVP_CIPHER* secrets_cipher()
{
    return SECRETS_CIPHER();
}

int secrets_keylen()
{
    return EVP_CIPHER_key_length(secrets_cipher());
}

int secrets_ivlen()
{
    return EVP_CIPHER_iv_length(secrets_cipher());
}

/**
 * Reads binary or text data from file and extracts the encryption key and, if old key format is used,
 * the init vector. The source file needs to have expected permissions. If the source file does not exist,
 * returns empty results.
 *
 * @param filepath Path to key file.
 * @return Result structure. Ok if file was loaded successfully or if file did not exist. False on error.
 */
ReadKeyResult secrets_readkeys(const string& filepath)
{
    ReadKeyResult rval;
    auto filepathc = filepath.c_str();

    const int binary_key_len = secrets_keylen();
    const int binary_iv_len = secrets_ivlen();
    const int binary_total_len = binary_key_len + binary_iv_len;

    // Before opening the file, check its size and permissions.
    struct stat filestats { 0 };
    bool stat_error = false;
    bool old_format = false;
    errno = 0;
    if (stat(filepathc, &filestats) == 0)
    {
        auto filesize = filestats.st_size;
        if (filesize == binary_total_len)
        {
            old_format = true;
            MXB_WARNING("File format of '%s' is deprecated. Please generate a new encryption key ('maxkeys') "
                        "and re-encrypt passwords ('maxpasswd').", filepathc);
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

    if (old_format)
    {
        errno = 0;
        std::ifstream file(filepath, std::ios_base::binary);
        if (file.is_open())
        {
            // Read all data from file.
            char readbuf[binary_total_len];
            file.read(readbuf, binary_total_len);
            if (file.good())
            {
                // Success, copy contents to key structure.
                rval.key.assign(readbuf, readbuf + binary_key_len);
                rval.iv.assign(readbuf + binary_key_len, readbuf + binary_total_len);
                rval.ok = true;
            }
            else
            {
                MXS_ERROR("Read from secrets file %s failed. Read %li, expected %i bytes. Error %d, %s.",
                          filepathc, file.gcount(), binary_total_len, errno, mxs_strerror(errno));
            }
        }
        else
        {
            MXS_ERROR("Could not open secrets file '%s'. Error %d, %s.",
                      filepathc, errno, mxs_strerror(errno));
        }
    }
    else
    {
        // File contents should be json.
        json_error_t err;
        json_t* obj = json_load_file(filepathc, 0, &err);
        if (obj)
        {
            const char* enc_cipher = json_string_value(json_object_get(obj, field_cipher));
            const char* enc_key = json_string_value(json_object_get(obj, field_key));
            bool cipher_ok = enc_cipher && (strcmp(enc_cipher, CIPHER_NAME) == 0);
            if (cipher_ok && enc_key)
            {
                int read_hex_key_len = strlen(enc_key);
                int expected_hex_key_len = 2 * binary_key_len;
                if (read_hex_key_len == expected_hex_key_len)
                {
                    rval.key.resize(binary_key_len);
                    mxs::hex2bin(enc_key, read_hex_key_len, rval.key.data());
                    rval.ok = true;
                }
                else
                {
                    MXB_ERROR("Wrong encryption key length in secrets file '%s': found %i, expected %i.",
                              filepathc, read_hex_key_len, expected_hex_key_len);
                }
            }
            else
            {
                MXB_ERROR("Secrets file '%s' does not contain expected string fields '%s' and '%s', "
                          "or '%s' is not '%s'.",
                          filepathc, field_cipher, field_key, field_cipher, CIPHER_NAME);
            }
            json_decref(obj);
        }
        else
        {
            MXB_ERROR("Error reading JSON from secrets file '%s': %s", filepathc, err.text);
        }
    }
    return rval;
}

namespace maxscale
{
string decrypt_password(const string& input)
{
    const auto& key = this_unit.key;
    string rval;
    if (key.empty())
    {
        // Password encryption is not used, return original.
        rval = input;
    }
    else
    {
        // If the input is not a HEX string, return the input as is.
        auto is_hex = std::all_of(input.begin(), input.end(), isxdigit);
        if (is_hex)
        {
            const auto& iv = this_unit.iv;
            rval = iv.empty() ? ::decrypt_password(key, input) : decrypt_password_old(key, iv, input);
        }
        else
        {
            rval = input;
        }
    }
    return rval;
}
}

/**
 * Decrypt passwords encrypted with an old (pre 2.5) .secrets-file. The decryption also depends on whether
 * the password was encrypted using maxpasswd 2.4 or 2.5.
 *
 * @param key Encryption key
 * @param iv Init vector
 * @param input Encrypted password in hex form
 * @return Decrypted password or empty on error
 */
string decrypt_password_old(const ByteVec& key, const ByteVec& iv, const std::string& input)
{
    string rval;
    // Convert to binary.
    size_t hex_len = input.length();
    auto bin_len = hex_len / 2;
    unsigned char encrypted_bin[bin_len];
    mxs::hex2bin(input.c_str(), hex_len, encrypted_bin);

    unsigned char plain[bin_len];   // Decryption output cannot be longer than input data.
    int decrypted_len = 0;
    if (encrypt_or_decrypt(key.data(), iv.data(), ProcessingMode::DECRYPT_IGNORE_ERRORS, encrypted_bin,
                           bin_len, plain, &decrypted_len))
    {
        if (decrypted_len > 0)
        {
            // Success, password was encrypted using 2.5. Decrypted data should be text.
            auto output_data = reinterpret_cast<const char*>(plain);
            rval.assign(output_data, decrypted_len);
        }
        else
        {
            // Failure, password was likely encrypted in 2.4. Try to decrypt using 2.4 code.
            AES_KEY aeskey;
            AES_set_decrypt_key(key.data(), 8 * key.size(), &aeskey);
            auto iv_copy = iv;
            memset(plain, '\0', bin_len);
            AES_cbc_encrypt(encrypted_bin, plain, bin_len, &aeskey, iv_copy.data(), AES_DECRYPT);
            rval = reinterpret_cast<const char*>(plain);
        }
    }
    return rval;
}

string decrypt_password(const ByteVec& key, const std::string& input)
{
    int total_hex_len = input.length();
    string rval;

    // Extract IV.
    auto ptr = input.data();
    int iv_bin_len = secrets_ivlen();
    int iv_hex_len = 2 * iv_bin_len;
    uint8_t iv_bin[iv_bin_len];
    if (total_hex_len >= iv_hex_len)
    {
        mxs::hex2bin(ptr, iv_hex_len, iv_bin);

        int encrypted_hex_len = total_hex_len - iv_hex_len;
        int encrypted_bin_len = encrypted_hex_len / 2;
        unsigned char encrypted_bin[encrypted_bin_len];
        mxs::hex2bin(ptr + iv_hex_len, encrypted_hex_len, encrypted_bin);

        uint8_t decrypted[encrypted_bin_len];   // Decryption output cannot be longer than input data.
        int decrypted_len = 0;
        if (encrypt_or_decrypt(key.data(), iv_bin, ProcessingMode::DECRYPT, encrypted_bin, encrypted_bin_len,
                               decrypted, &decrypted_len))
        {
            // Decrypted data should be text.
            auto output_data = reinterpret_cast<const char*>(decrypted);
            rval.assign(output_data, decrypted_len);
        }
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
string encrypt_password_old(const ByteVec& key, const ByteVec& iv, const string& input)
{
    string rval;
    // Output can be a block length longer than input.
    auto input_len = input.length();
    unsigned char encrypted_bin[input_len + AES_BLOCK_SIZE];

    // Although input is text, interpret as binary.
    auto input_data = reinterpret_cast<const uint8_t*>(input.c_str());
    int encrypted_len = 0;
    if (encrypt_or_decrypt(key.data(), iv.data(), ProcessingMode::ENCRYPT,
                           input_data, input_len, encrypted_bin, &encrypted_len))
    {
        int hex_len = 2 * encrypted_len;
        char hex_output[hex_len + 1];
        mxs::bin2hex(encrypted_bin, encrypted_len, hex_output);
        rval.assign(hex_output, hex_len);
    }
    return rval;
}

string encrypt_password(const ByteVec& key, const string& input)
{
    string rval;
    // Generate random IV.
    auto ivlen = secrets_ivlen();
    unsigned char iv_bin[ivlen];
    if (RAND_bytes(iv_bin, ivlen) != 1)
    {
        printf("OpenSSL RAND_bytes() failed. %s.\n", ERR_error_string(ERR_get_error(), nullptr));
        return rval;
    }

    // Output can be a block length longer than input.
    auto input_len = input.length();
    unsigned char encrypted_bin[input_len + EVP_CIPHER_block_size(secrets_cipher())];

    // Although input is text, interpret as binary.
    auto input_data = reinterpret_cast<const uint8_t*>(input.c_str());
    int encrypted_len = 0;
    if (encrypt_or_decrypt(key.data(), iv_bin, ProcessingMode::ENCRYPT,
                           input_data, input_len, encrypted_bin, &encrypted_len))
    {
        // Form one string with IV in front.
        int iv_hex_len = 2 * ivlen;
        int encrypted_hex_len = 2 * encrypted_len;
        int total_hex_len = iv_hex_len + encrypted_hex_len;
        char hex_output[total_hex_len + 1];
        mxs::bin2hex(iv_bin, ivlen, hex_output);
        mxs::bin2hex(encrypted_bin, encrypted_len, hex_output + iv_hex_len);
        rval.assign(hex_output, total_hex_len);
    }
    return rval;
}

bool load_encryption_keys()
{
    mxb_assert(this_unit.key.empty() && this_unit.iv.empty());

    string path(mxs::datadir());
    path.append("/").append(SECRETS_FILENAME);
    auto ret = secrets_readkeys(path);
    if (ret.ok)
    {
        if (!ret.key.empty())
        {
            MXB_NOTICE("Using encrypted passwords. Encryption key read from '%s'.", path.c_str());
            this_unit.key = move(ret.key);
            this_unit.iv = move(ret.iv);
        }
        else
        {
            MXB_NOTICE("Password encryption key file '%s' not found, using configured passwords as "
                       "plaintext.", path.c_str());
        }
    }
    return ret.ok;
}

/**
 * Write encryption key to JSON-file. Also sets file permissions and owner.
 *
 * @param key Encryption key in binary form
 * @param filepath The full path to the file to write to
 * @param owner The final owner of the file. Changing the owner does not always succeed.
 * @return True on total success. Even if false is returned, the file may have been written.
 */
bool secrets_write_keys(const ByteVec& key, const string& filepath, const string& owner)
{
    auto keylen = key.size();
    char key_hex[2 * keylen + 1];
    mxs::bin2hex(key.data(), keylen, key_hex);

    json_t* obj = json_object();
    json_object_set_new(obj, field_desc, json_string(desc));
    json_object_set_new(obj, field_version, json_string(MAXSCALE_VERSION));
    json_object_set_new(obj, field_cipher, json_string(CIPHER_NAME));
    json_object_set_new(obj, field_key, json_string(key_hex));

    auto filepathc = filepath.c_str();
    bool write_ok = false;
    errno = 0;
    if (json_dump_file(obj, filepathc, JSON_INDENT(4)) == 0)
    {
        write_ok = true;
    }
    else
    {
        printf("Write to secrets file '%s' failed. Error %d, %s.\n",
               filepathc, errno, mxs_strerror(errno));
    }
    json_decref(obj);

    bool rval = false;
    if (write_ok)
    {
        // Change file permissions to prevent modifications.
        errno = 0;
        if (chmod(filepathc, S_IRUSR) == 0)
        {
            printf("Permissions of '%s' set to owner:read.\n", filepathc);
            auto ownerz = owner.c_str();
            auto userinfo = getpwnam(ownerz);
            if (userinfo)
            {
                if (chown(filepathc, userinfo->pw_uid, userinfo->pw_gid) == 0)
                {
                    printf("Ownership of '%s' given to %s.\n", filepathc, ownerz);
                    rval = true;
                }
                else
                {
                    printf("Failed to give '%s' ownership of '%s': %d, %s.\n",
                           ownerz, filepathc, errno, mxb_strerror(errno));
                }
            }
            else
            {
                printf("Could not find user '%s' when attempting to change ownership of '%s': %d, %s.\n",
                       ownerz, filepathc, errno, mxb_strerror(errno));
            }
        }
        else
        {
            printf("Failed to change the permissions of the secrets file '%s'. Error %d, %s.\n",
                   filepathc, errno, mxs_strerror(errno));
        }
    }
    return rval;
}

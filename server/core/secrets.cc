/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
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
#include <openssl/ossl_typ.h>
#include <openssl/rand.h>

#include <maxbase/format.hh>
#include <maxscale/paths.hh>
#include <maxscale/utils.hh>
#include <maxscale/version.hh>
#include <maxbase/secrets.hh>

#include "internal/secrets.hh"

using std::string;

const char* const SECRETS_FILENAME = ".secrets";

namespace
{

struct ThisUnit
{
    ByteVec     key;/**< Password decryption key, assigned at startup */
    mxb::Cipher cipher {SECRETS_CIPHER_MODE, SECRETS_CIPHER_BITS};
};
ThisUnit this_unit;

const char field_desc[] = "description";
const char field_version[] = "maxscale_version";
const char field_cipher[] = "encryption_cipher";
const char field_key[] = "encryption_key";
const char desc[] = "MaxScale encryption/decryption key";

// Note: this must be EVP_aes_256_cbc, otherwise the code discards the key as invalid.
const char CIPHER_NAME[] = "EVP_aes_256_cbc";
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

    // Old files stored their data as the concatenation of the private key (32 bytes) and the IV (16 bytes).
    const int binary_key_len = EVP_CIPHER_key_length(EVP_aes_256_cbc());
    const int binary_iv_len = EVP_CIPHER_iv_length(EVP_aes_256_cbc());
    const int binary_total_len = binary_key_len + binary_iv_len;
    mxb_assert(binary_total_len == 32 + 16);

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
        }

        auto filemode = filestats.st_mode;
        if (!S_ISREG(filemode))
        {
            MXB_ERROR("Secrets file '%s' is not a regular file.", filepathc);
            stat_error = true;
        }
        else if ((filemode & (S_IRWXU | S_IRWXG | S_IRWXO)) != S_IRUSR)
        {
            MXB_ERROR("Secrets file '%s' permissions are wrong. The only permission on the file should be "
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
        MXB_ERROR("stat() for secrets file '%s' failed. Error %d, %s.",
                  filepathc, errno, mxb_strerror(errno));
        stat_error = true;
    }

    if (stat_error)
    {
        return rval;
    }

    if (old_format)
    {
        MXB_ERROR("File format of '%s' is using a pre-2.5 format that is no longer supported. "
                  "Please generate a new encryption key ('maxkeys') and re-encrypt "
                  "passwords ('maxpasswd').", filepathc);
        return rval;
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
            rval = mxs::decrypt_password(key, input);
        }
        else
        {
            rval = input;
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
    int iv_bin_len = this_unit.cipher.iv_size();
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
        if (this_unit.cipher.decrypt(key.data(), iv_bin, encrypted_bin, encrypted_bin_len,
                                     decrypted, &decrypted_len))
        {
            // Decrypted data should be text.
            auto output_data = reinterpret_cast<const char*>(decrypted);
            rval.assign(output_data, decrypted_len);
        }
        else
        {
            this_unit.cipher.log_errors("when decrypting password");
        }
    }

    return rval;
}

string encrypt_password(const ByteVec& key, const string& input)
{
    string rval;
    // Generate random IV.
    auto iv = this_unit.cipher.new_iv();

    if (iv.empty())
    {
        return rval;
    }

    // Output can be a block length longer than input.
    auto input_len = input.length();
    unsigned char encrypted_bin[input_len + this_unit.cipher.block_size()];

    // Although input is text, interpret as binary.
    auto input_data = reinterpret_cast<const uint8_t*>(input.c_str());
    int encrypted_len = 0;

    if (this_unit.cipher.encrypt(key.data(), iv.data(), input_data, input_len, encrypted_bin, &encrypted_len))
    {
        // Form one string with IV in front.
        int iv_hex_len = 2 * iv.size();
        int encrypted_hex_len = 2 * encrypted_len;
        int total_hex_len = iv_hex_len + encrypted_hex_len;
        char hex_output[total_hex_len + 1];
        mxs::bin2hex(iv.data(), iv.size(), hex_output);
        mxs::bin2hex(encrypted_bin, encrypted_len, hex_output + iv_hex_len);
        rval.assign(hex_output, total_hex_len);
    }
    else
    {
        this_unit.cipher.log_errors("when encrypting password");
    }

    return rval;
}
}

bool load_encryption_keys()
{
    mxb_assert(this_unit.key.empty());

    string path(mxs::datadir());
    path.append("/").append(SECRETS_FILENAME);
    auto ret = secrets_readkeys(path);
    if (ret.ok)
    {
        if (!ret.key.empty())
        {
            MXB_NOTICE("Using encrypted passwords. Encryption key read from '%s'.", path.c_str());
            this_unit.key = move(ret.key);
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
               filepathc, errno, mxb_strerror(errno));
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
                   filepathc, errno, mxb_strerror(errno));
        }
    }
    return rval;
}

/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-02
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file maxpasswd.c  - Implementation of pasword encoding
 */

#include <maxscale/ccdefs.hh>

#include <cstdio>
#include <getopt.h>
#include <maxbase/log.hh>
#include <maxscale/paths.hh>

#include "internal/secrets.hh"

using std::string;

struct option options[] =
{
    {"help",    no_argument, nullptr, 'h'},
    {"decrypt", no_argument, nullptr, 'd'},
    {nullptr,   0,           nullptr, 0  }
};

void print_usage(const char* executable, const char* directory)
{
    const char msg[] =
        R"(Usage: %s [-h|--help] [path] password

Encrypt a MaxScale plaintext password using the encryption key in the key file
'%s'. The key file may be generated using the 'maxkeys'-utility.

  -h, --help    Display this help.
  -d, --decrypt Decrypt an encrypted password instead

  path      The key file directory (default: '%s')
  password  The password to encrypt or decrypt
)";
    printf(msg, executable, SECRETS_FILENAME, directory);
}

int main(int argc, char** argv)
{
    mxb::Log log(MXB_LOG_TARGET_STDOUT);
    const char* default_directory = mxs::datadir();

    enum class Mode
    {
        ENCRYPT,
        DECRYPT
    };

    auto mode = Mode::ENCRYPT;

    int c;
    while ((c = getopt_long(argc, argv, "hd", options, NULL)) != -1)
    {
        switch (c)
        {
        case 'h':
            print_usage(argv[0], default_directory);
            return EXIT_SUCCESS;

        case 'd':
            mode = Mode::DECRYPT;
            break;

        default:
            print_usage(argv[0], default_directory);
            return EXIT_FAILURE;
        }
    }

    string input;
    string path = default_directory;

    switch (argc - optind)
    {
    case 2:
        // Two args provided.
        path = argv[optind];
        input = argv[optind + 1];
        break;

    case 1:
        // One arg provided.
        input = argv[optind];
        break;

    default:
        print_usage(argv[0], default_directory);
        return EXIT_FAILURE;
    }

    int rval = EXIT_FAILURE;
    string filepath = path;
    filepath.append("/").append(SECRETS_FILENAME);

    auto keydata = secrets_readkeys(filepath);
    if (keydata.ok)
    {
        bool encrypting = (mode == Mode::ENCRYPT);
        bool new_mode = keydata.iv.empty();     // false -> constant IV from file
        if (keydata.key.empty())
        {
            printf("Password encryption key file '%s' not found, cannot %s password.\n",
                   filepath.c_str(), encrypting ? "encrypt" : "decrypt");
        }
        else if (encrypting)
        {
            string encrypted = new_mode ? encrypt_password(keydata.key, input) :
                encrypt_password_old(keydata.key, keydata.iv, input);
            if (!encrypted.empty())
            {
                printf("%s\n", encrypted.c_str());
                rval = EXIT_SUCCESS;
            }
            else
            {
                printf("Password encryption failed.\n");
            }
        }
        else
        {
            auto is_hex = std::all_of(input.begin(), input.end(), isxdigit);
            if (is_hex && input.length() % 2 == 0)
            {
                string decrypted = new_mode ? decrypt_password(keydata.key, input) :
                    decrypt_password_old(keydata.key, keydata.iv, input);
                if (!decrypted.empty())
                {
                    printf("%s\n", decrypted.c_str());
                    rval = EXIT_SUCCESS;
                }
                else
                {
                    printf("Password decryption failed.\n");
                }
            }
            else
            {
                printf("Input is not a valid hex-encoded encrypted password.\n");
            }
        }
    }
    else
    {
        printf("Could not read encryption key file '%s'.\n", filepath.c_str());
    }
    return rval;
}

/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
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
#include <termios.h>
#include <unistd.h>
#include <maxbase/log.hh>
#include <maxscale/paths.hh>
#include <iostream>

#include "internal/secrets.hh"

using std::cin;
using std::cout;
using std::endl;
using std::flush;
using std::string;

struct option options[] =
{
    {"help",        no_argument, nullptr, 'h'},
    {"decrypt",     no_argument, nullptr, 'd'},
    {"interactive", no_argument, nullptr, 'i'},
    {nullptr,       0,           nullptr, 0  }
};

void print_usage(const char* executable, const char* directory)
{
    const char msg[] =
        R"(Usage: %s [-h|--help] [-i|--interactive] [-d|--decrypt] [path] password

Encrypt a MaxScale plaintext password using the encryption key in the key file
'%s'. The key file may be generated using the 'maxkeys'-utility.

  -h, --help         Display this help.
  -d, --decrypt      Decrypt an encrypted password instead.
  -i, --interactive  - If maxpasswd is reading from a pipe, it will read a line and
                       use that as the password.
                     - If maxpasswd is connected to a terminal console, it will prompt
                       for the password.
                     If '-i' is specified, a single argument is assumed to be the path
                     and two arguments is treated like an error.

  path      The key file directory (default: '%s')
  password  The password to encrypt or decrypt
)";

    printf(msg, executable, SECRETS_FILENAME, directory);
}

bool read_password(string* pPassword)
{
    bool rv = false;
    string password;

    if (isatty(STDIN_FILENO))
    {
        struct termios tty;
        tcgetattr(STDIN_FILENO, &tty);

        bool echo = (tty.c_lflag & ECHO);
        if (echo)
        {
            tty.c_lflag &= ~ECHO;
            tcsetattr(STDIN_FILENO, TCSANOW, &tty);
        }

        cout << "Enter password : " << flush;
        string s1;
        std::getline(std::cin, s1);
        cout << endl;

        cout << "Repeat password: " << flush;
        string s2;
        std::getline(std::cin, s2);
        cout << endl;

        if (echo)
        {
            tty.c_lflag |= ECHO;
            tcsetattr(STDIN_FILENO, TCSANOW, &tty);
        }

        if (s1 == s2)
        {
            password = s1;
            rv = true;
        }
        else
        {
            cout << "Passwords are not identical." << endl;
        }
    }
    else
    {
        std::getline(std::cin, password);
        rv = true;
    }

    if (rv)
    {
        *pPassword = password;
    }

    return rv;
}


int main(int argc, char** argv)
{
    std::ios::sync_with_stdio();

    mxb::Log log(MXB_LOG_TARGET_STDOUT);
    const char* default_directory = mxs::datadir();

    enum class Mode
    {
        ENCRYPT,
        DECRYPT
    };

    auto mode = Mode::ENCRYPT;
    bool interactive = false;

    int c;
    while ((c = getopt_long(argc, argv, "hdi", options, NULL)) != -1)
    {
        switch (c)
        {
        case 'h':
            print_usage(argv[0], default_directory);
            return EXIT_SUCCESS;

        case 'd':
            mode = Mode::DECRYPT;
            break;

        case 'i':
            interactive = true;
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
        if (!interactive)
        {
            input = argv[optind + 1];
        }
        else
        {
            print_usage(argv[0], default_directory);
            return EXIT_FAILURE;
        }
        break;

    case 1:
        // One arg provided.
        if (!interactive)
        {
            input = argv[optind];
        }
        else
        {
            path = argv[optind];
        }
        break;

    case 0:
        if (!interactive)
        {
            print_usage(argv[0], default_directory);
            return EXIT_FAILURE;
        }
        break;

    default:
        print_usage(argv[0], default_directory);
        return EXIT_FAILURE;
    }

    if (interactive)
    {
        if (!read_password(&input))
        {
            return EXIT_FAILURE;
        }
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

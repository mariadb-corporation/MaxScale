/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <chrono>
#include <cstdio>
#include <getopt.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <maxbase/log.hh>
#include <maxscale/paths.hh>
#include "internal/secrets.hh"

using std::string;
using ByteVec = std::vector<uint8_t>;

struct option options[] =
{
    {"help",  no_argument,       nullptr, 'h'},
    {"user",  required_argument, nullptr, 'u'},
    {nullptr, 0,                 nullptr, 0  }
};

const string default_user = "maxscale";

ByteVec generate_encryption_key();

void print_usage(const char* executable, const char* default_directory)
{
    const char msg[] =
        R"(usage: %s [-h|--help] [directory]

This utility generates a random AES encryption key and init vector and writes
them to disk. The data is written to the file '%s', in the specified
directory. The key and init vector are used by the utility 'maxpasswd' to
encrypt passwords used in MaxScale configuration files, as well as by MaxScale
itself to decrypt the passwords.

Re-creating the file invalidates all existing encrypted passwords in the
configuration files.

  -h, --help    Display this help
  -u, --user    Designate the owner of the generated file (default: '%s')

  directory  : The directory where to store the file in (default: '%s')
)";
    printf(msg, executable, SECRETS_FILENAME, default_user.c_str(), default_directory);
}

int main(int argc, char** argv)
{
    mxb::Log log(MXB_LOG_TARGET_STDOUT);
    const string default_directory = mxs::datadir();
    string username = default_user;

    int c;
    while ((c = getopt_long(argc, argv, "hu:", options, nullptr)) != -1)
    {
        switch (c)
        {
        case 'h':
            print_usage(argv[0], default_directory.c_str());
            return EXIT_SUCCESS;

        case 'u':
            username = optarg;
            break;

        default:
            print_usage(argv[0], default_directory.c_str());
            return EXIT_FAILURE;
        }
    }

    string filepath = default_directory;
    if (optind < argc)
    {
        filepath = argv[optind];
    }
    filepath.append("/").append(SECRETS_FILENAME);

    // Check that the file doesn't exist.
    errno = 0;
    auto filepathc = filepath.c_str();
    if (access(filepathc, F_OK) == 0)
    {
        printf("Secrets file '%s' already exists. Delete it before generating a new encryption key.\n",
               filepathc);
        return EXIT_FAILURE;
    }
    else if (errno != ENOENT)
    {
        printf("stat() for secrets file '%s' failed unexpectedly. Error %i, %s.\n",
               filepathc, errno, mxb_strerror(errno));
        return EXIT_FAILURE;
    }

    int rval = EXIT_FAILURE;
    auto new_key = generate_encryption_key();
    if (!new_key.empty() && secrets_write_keys(new_key, filepath, username))
    {
        rval = EXIT_SUCCESS;
    }
    return rval;
}

ByteVec generate_encryption_key()
{
    int keylen = EVP_CIPHER_key_length(secrets_cipher());
    ByteVec key(keylen);
    // Generate random bytes using OpenSSL.
    if (RAND_bytes(key.data(), keylen) != 1)
    {
        auto errornum = ERR_get_error();
        printf("OpenSSL RAND_bytes() failed. %s.\n", ERR_error_string(errornum, nullptr));
        key.clear();
    }
    return key;
}

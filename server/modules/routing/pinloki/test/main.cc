/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "../writer.hh"
#include "../file_writer.hh"
#include "../reader.hh"
#include "../config.hh"
#include "../gtid.hh"
#include "../find_gtid.hh"

#include <maxbase/maxbase.hh>
#include <maxbase/hexdump.hh>
#include <maxbase/log.hh>
#include <maxbase/exception.hh>

#include <thread>
#include <iostream>
#include <iomanip>
#include <getopt.h>

pinloki::Config& config()
{
    static pinloki::Config cfg("test");
    return cfg;
}

pinloki::InventoryWriter& write_inventory()
{
    static pinloki::InventoryWriter inv(config());
    return inv;
}

// for apropos tests
bool test_it(int argc, char* argv[])
{
    return false;
}

using namespace std::literals::chrono_literals;
using namespace pinloki;

bool writer_mode = true;

void prog_main(const maxsql::GtidList& gtid_list,
    const std::string& host,
    const std::string& user,
    const std::string& pw)
{
    mxb::Worker worker;
    mxq::Connection::ConnectionDetails details = {maxbase::Host::from_string(host), "", user, pw};

    if (writer_mode)
    {
        pinloki::Writer writer(details, &write_inventory());
        worker.start();
        worker.join();
    }
    else
    {
        pinloki::Reader reader(
            [](const auto& event) {
            std::cout << event << std::endl;
            return true;
            },
            [&worker]() -> mxb::Worker& { return worker; },
            config(),
            gtid_list,
            30s);

        worker.start();
        worker.execute([&reader] { reader.start(); }, mxb::Worker::EXECUTE_QUEUED);
        worker.join();
    }
}

int main(int argc, char* argv[])
try
{
    mxb::MaxBase mxb(MXB_LOG_TARGET_STDOUT);

    mxs_log_set_priority_enabled(LOG_DEBUG, true);

    if (test_it(argc, argv))
    {
        return EXIT_SUCCESS;
    }

    bool help = false;
    std::string mode;
    maxsql::GtidList override_gtid_list;
    int port         = 4000;
    std::string host = "127.0.0.1";
    std::string user = "maxskysql";
    std::string pw   = "skysql";

    static struct option long_options[] = {{"help", no_argument, nullptr, '?'},
        {"mode", required_argument, nullptr, 'm'},
        {"gtid", required_argument, nullptr, 'g'},
        {"port", required_argument, nullptr, 'P'},
        {"host", required_argument, nullptr, 'h'},
        {"user", required_argument, nullptr, 'u'},
        {"password", required_argument, nullptr, 'p'},
        {nullptr, 0, nullptr, 0}};

    while (1)
    {
        int option_index = 0;

        int c = getopt_long(argc, argv, "?m:g:P:h:u:p:", long_options, &option_index);
        if (c == -1)
        {
            break;
        }

        switch (c)
        {
        case '?':
            help = true;
            break;

        case 'm':
        {
            std::string mode = optarg;
            help             = !(mode == "writer" || mode == "reader");
            writer_mode      = mode == "writer";
        }
        break;

        case 'g':
        {
            override_gtid_list = maxsql::GtidList::from_string(optarg);
            if (!override_gtid_list.is_valid())
            {
                help = true;
                std::cerr << "The provided gtid override is invalid: " << optarg << '\n';
            }
        }
        break;

        case 'P':
            port = atoi(optarg);
            break;

        case 'h':
            host = optarg;
            break;

        case 'u':
            user = optarg;
            break;

        case 'p':
            pw = optarg;
            break;

        default:
            help = true;
        }
    }

    if (help)
    {
        std::cout << "-h --help\t" << std::boolalpha << help;

        std::cout << "\n-r --reset\t" << std::boolalpha << "\n\t\tWhen set, clears all binlog files";

        std::cout << "\n-m --mode\tmode='" << (writer_mode ? "writer'" : "reader'")
                  << "\n\t\tOptions are 'writer' and 'reader'";

        std::cout << "\n-g --gtid\t"
                  << (override_gtid_list.is_valid() ? override_gtid_list.to_string() : "No gtid override");

        std::cout << "\n-h --host\t" << host;
        std::cout << "\n-P --port\t" << port;
        std::cout << "\n-u --user\t" << user;
        std::cout << "\n-p --password\t" << (pw.empty() ? "" : "*****");
        std::cout << std::endl;

        return EXIT_SUCCESS;
    }

    prog_main(override_gtid_list, host + ":" + std::to_string(port), user, pw);
}

catch (maxbase::Exception& ex)
{
    // TODO swap Exception::what() and Exception::error_msg()
    std::cerr << ex.error_msg() << std::endl;
}

catch (std::exception& ex)
{
    std::cerr << ex.what() << std::endl;
}

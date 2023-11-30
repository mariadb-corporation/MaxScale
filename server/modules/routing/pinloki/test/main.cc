/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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

using namespace pinloki;

Config& config()
{
    static Config cfg("test", []() {
        return true;
    });
    return cfg;
}

InventoryWriter& write_inventory()
{
    static InventoryWriter inv(config());
    return inv;
}

bool writer_mode = true;

void prog_main(int nthreads, const maxsql::GtidList& gtid_list, const std::string& host,
               const std::string& user, const std::string& pw)
{
    mxq::Connection::ConnectionDetails details = {maxbase::Host::from_string(host), "", user, pw};
    config().post_configure({});

    if (writer_mode)
    {
        mxb::Worker worker;
        Writer writer(details, &write_inventory());
        worker.start("Writer");
        worker.join();
    }
    else
    {
        maxbase::StopWatch sw;
        auto latest = find_last_gtid_list(config());
        std::cout << "find_last_gtid_list: " << mxb::to_string(sw.split()) << std::endl;

        config().save_rpl_state(latest);

        sw.restart();

        auto send_callback = [&sw](const maxsql::RplEvent& event){
            if (event.event_type() == GTID_EVENT
                && event.gtid_event().gtid.sequence_nr() % 10000 == 0)
            {
                std::cout << event.gtid_event().gtid.sequence_nr() << " " << mxb::to_string(sw.split()) <<
                std::endl;
            }
            return true;
        };

        auto abort_callback = []{
            throw std::runtime_error("Abort callback");
        };

        std::vector<std::unique_ptr<mxb::Worker>> workers;
        std::vector<std::unique_ptr<Reader>> readers;
        workers.reserve(nthreads);
        readers.reserve(nthreads);

        for (ssize_t i = 0; i < nthreads; ++i)
        {
            workers.push_back(std::make_unique<mxb::Worker>());
            auto& worker = *workers.back();
            worker.start(MAKE_STR("Worker " << worker.id()));

            auto worker_callback = [&worker]()-> mxb::Worker& {
                return worker;
            };

            readers.push_back(std::make_unique<Reader>(
                send_callback,
                worker_callback,
                abort_callback,
                config(), gtid_list, 30s));

            auto& reader = *readers.back();

            worker.execute([&reader]{
                reader.start();
            }, mxb::Worker::EXECUTE_QUEUED);
        }

        for (auto& sWorker : workers)
        {
            sWorker->join();
        }
    }
}

int main(int argc, char* argv[])
try
{
    mxb::MaxBase mxb(MXB_LOG_TARGET_STDOUT);

    mxb_log_set_priority_enabled(LOG_INFO, true);

    bool help = false;
    int nthreads = 1;
    std::string mode;
    maxsql::GtidList override_gtid_list;
    int port = 4000;
    std::string host = "127.0.0.1";
    std::string user = "maxskysql";
    std::string pw = "skysql";

    static struct option long_options[] = {
        {"help",     no_argument,           nullptr,                 '?'},
        {"mode",     required_argument,     nullptr,                 'm'},
        {"threads",  required_argument,     nullptr,                 't'},
        {"gtid",     required_argument,     nullptr,                 'g'},
        {"port",     required_argument,     nullptr,                 'P'},
        {"host",     required_argument,     nullptr,                 'h'},
        {"user",     required_argument,     nullptr,                 'u'},
        {"password", required_argument,     nullptr,                 'p'},
        {nullptr,    0,                     nullptr,                 0  }
    };

    while (1)
    {
        int option_index = 0;

        int c = getopt_long(argc, argv, "?m:t:g:P:h:u:p:", long_options, &option_index);
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
                std::string mod = optarg;
                help = !(mod == "writer" || mod == "reader");
                writer_mode = mod == "writer";
            }
            break;

        case 't':
            nthreads = atoi(optarg);
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

        std::cout << "\n-m --mode\tmode='" << (writer_mode ? "writer'" : "reader'")
                  << "\n\t\tOptions are 'writer' and 'reader'";

        std::cout << "\n-t --threads\t" << nthreads
                  << "\n\t\tNumber of threads/workers when mode is reader";

        std::cout << "\n-g --gtid\t"
                  << (override_gtid_list.is_valid() ? override_gtid_list.to_string() : "No gtid override");

        std::cout << "\n-h --host\t" << host;
        std::cout << "\n-P --port\t" << port;
        std::cout << "\n-u --user\t" << user;
        std::cout << "\n-p --password\t" << (pw.empty() ? "" : "*****");
        std::cout << std::endl;

        return EXIT_SUCCESS;
    }

    prog_main(nthreads, override_gtid_list, host + ":" + std::to_string(port), user, pw);
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

#include "writer.hh"
#include "file_writer.hh"
#include "reader.hh"
#include "config.hh"
#include "gtid.hh"
#include "find_gtid.hh"

#include <maxbase/hexdump.hh>
#include <maxbase/log.hh>
#include <maxbase/exception.hh>

#include <thread>
#include <iostream>
#include <iomanip>
#include <getopt.h>

// for apropos tests
bool test_it(int argc, char* argv[])
{
    return false;

    auto gtid = maxsql::Gtid::from_string("0-0-9");
    pinloki::GtidPosition pos = pinloki::find_gtid_position(gtid);

    std::cout << "pos.file_name = " << pos.file_name << "\n";
    std::cout << "pos.pos = " << pos.file_pos << "\n";

    return true;
}

using namespace std::literals::chrono_literals;
using namespace pinloki;

bool writer_mode = true;

void prog_main(const maxsql::GtidList& gtid_list)
{
    // Single domain currently
    maxsql::Gtid gtid;
    if (gtid_list.is_valid())
    {
        gtid = gtid_list.gtids()[0];
    }

    if (writer_mode)
    {
        pinloki::Writer writer;
        std::thread wthread;
        wthread = std::thread(&pinloki::Writer::run, &writer);
        wthread.join();
    }
    else
    {
        pinloki::Reader reader(gtid);
        std::thread rthread;
        rthread = std::thread(&pinloki::Reader::run, &reader);
        rthread.join();
    }
}

int main(int argc, char* argv[])
try
{
    mxb_log_init();

    if (test_it(argc, argv))
    {
        return EXIT_SUCCESS;
    }

    bool help = false;
    std::string mode;
    maxsql::GtidList override_gtid_list;

    static struct option long_options[] = {
        {"help",  no_argument,       nullptr, 0},
        {"mode",  required_argument, nullptr, 0},
        {"gtid",  required_argument, nullptr, 0},
        {nullptr, 0,                 nullptr, 0}
    };

    while (1)
    {
        int option_index = 0;

        int c = getopt_long(argc, argv, "hm:g:", long_options, &option_index);
        if (c == -1)
        {
            break;
        }

        switch (c)
        {
        case 'h':
            help = true;
            break;

        case 'm':
            {
                std::string mode = optarg;
                help = !(mode == "writer" || mode == "reader");
                writer_mode = mode == "writer";
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

        default:
            help = true;
        }
    }

    if (help)
    {
        std::cout << "-h --help\t" << std::boolalpha << help;

        std::cout << "\n-r --reset\t" << std::boolalpha
                  << "\n\t\tWhen set, clears all binlog files";

        std::cout << "\n-m --mode\tmode='" << (writer_mode ? "writer'" : "reader'")
                  << "\n\t\tOptions are 'writer' and 'reader'";

        std::cout << "\n-g --gtid\t"
                  << (override_gtid_list.is_valid() ? override_gtid_list.to_string() : "No gtid override");
        std::cout << std::endl;

        return EXIT_SUCCESS;
    }

    if (override_gtid_list.is_valid())
    {
        std::ofstream ofs(config().gtid_file_path());
        ofs << override_gtid_list;
    }

    prog_main(override_gtid_list);
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

/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "../mxsmongo.hh"
#include <string.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using namespace std;

namespace
{

void print_usage_and_exit(const char* zName)
{
    cout << "usage: " << zName << " file.hex" << endl;
    exit(EXIT_FAILURE);
}

void analyze(const vector<uint8_t>& buffer)
{
    mxsmongo::Packet packet(buffer);

    switch (packet.opcode())
    {
    case mxsmongo::Packet::QUERY:
        {
            mxsmongo::Query query(packet);
            cout << query << endl;
        }
        break;

    case mxsmongo::Packet::REPLY:
        {
            mxsmongo::Reply reply(packet);
            cout << reply << endl;
        }
        break;

    case mxsmongo::Packet::MSG:
        {
            mxsmongo::Msg msg(packet);
            cout << msg << endl;
        }
        break;

    default:
        mxb_assert(!true);
    }
}

vector<uint8_t> create_packet(const char* zPacket, size_t nPacket)
{
    const char* z = zPacket;
    const char* end = zPacket + nPacket;

    vector<uint8_t> buffer;

    string word;
    while (z != end)
    {
        string hex;
        hex += *z;

        ++z;
        mxb_assert(z != end);

        hex += *z;

        uint16_t value = std::stoi(hex, nullptr, 16);

        buffer.push_back(value & 0xff);

        ++z;
    }

    return buffer;
}

vector<uint8_t> create_packet(const string& line)
{
    return create_packet(line.c_str(), line.length());
}

}


int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        print_usage_and_exit(argv[0]);
    }

    ifstream in(argv[1]);

    if (in)
    {
        string line;

        while (in)
        {
            getline(in, line);

            if (!line.empty())
            {
                analyze(create_packet(line.c_str()));
            }
        }
    }

    return 0;
}

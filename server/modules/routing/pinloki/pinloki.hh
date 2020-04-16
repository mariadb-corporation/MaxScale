#pragma once

#include <string>
#include <array>
#include <maxbase/exception.hh>

namespace pinloki
{
DEFINE_EXCEPTION(BinlogReadError);

static std::array<char, 4> PINLOKI_MAGIC = {char(0xfe), 0x62, 0x69, 0x6e};

struct FileLocation
{
    std::string file_name;
    long        loc;
};
}

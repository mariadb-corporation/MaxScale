#pragma once

#include "gtid.hh"
namespace pinloki
{

struct GtidPosition
{
    std::string file_name;
    long        file_pos;
};

// Default constructed GtidPosition if not found.
GtidPosition find_gtid_position(const maxsql::Gtid& gtid);
}

/*
Copyright (C) 2013-2014, MariaDB Corporation Ab


This file is distributed as part of the MariaDB Corporation MaxScale. It is free
software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation,
version 2.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 51
Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

Author: Jan Lindström jan.lindstrom@mariadb.com

*/

#ifndef REPLICATION_LISTENER_MYSQL_ERROR_EXCEPTION
#define REPLICATION_LISTENER_MYSQL_ERROR_EXCEPTION

namespace mysql
{

// Derive from std::runtime_error rather than std::exception
// runtime_error's constructor can take a string as parameter
// the standard's compliant version of std::exception can not
// (though some compiler provide a non standard constructor).
//

#include <sstream>
#include <boost/system/system_error.hpp>

// Helper function
template <class T>
inline std::string to_string (const T& t)
{
    std::stringstream ss;
    ss << t;
    return ss.str();
}

class ListenerException : public std::runtime_error
{
    public:

    ListenerException(std::string message, const char *file, int line)
      : std::runtime_error(std::string("Exception: ") + message + std::string(" file: ") + std::string(file) + std::string(" line: ") + (to_string(line))) {}

};

}

#endif

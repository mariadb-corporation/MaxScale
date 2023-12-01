/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/format.hh>
#include <maxbase/string.hh>

#include <fstream>

namespace maxbase
{

/**
 * Load a file from disk
 *
 * @param file The path to the file to load
 *
 * @return The file contents if the file was loaded successfully or an error message if it wasn't
 */
template<class Container>
std::pair<Container, std::string> load_file(const std::string& file)
{
    static_assert(sizeof(typename Container::iterator::value_type) == sizeof(char));
    std::string err;
    Container data;
    std::ifstream infile(file, std::ios_base::ate | std::ios_base::binary);

    if (infile)
    {
        data.resize(infile.tellg());
        infile.seekg(0, std::ios_base::beg);

        if (!infile.read(reinterpret_cast<char*>(data.data()), data.size()))
        {
            err = mxb::string_printf("Failed to read from file '%s': %d, %s",
                                     file.c_str(), errno, mxb_strerror(errno));
            data.clear();
        }
    }
    else
    {
        err = mxb::string_printf("Failed to open file '%s': %d, %s",
                                 file.c_str(), errno, mxb_strerror(errno));
    }

    return {std::move(data), std::move(err)};
}

/**
 * Atomically save a file ok disk
 *
 * The atomicity is achieved by writing out the data into a temporary file and then renaming it.
 *
 * @param file The path where the file is saved
 * @param ptr  Pointer to data
 * @param size Size of the data
 *
 * @return An empty string if the file was saved successfully or an error message if it wasn't
 */
std::string save_file(std::string file, const void* ptr, size_t size);

inline std::string save_file(std::string file, const std::string& str)
{
    return save_file(file, str.data(), str.size());
}
}

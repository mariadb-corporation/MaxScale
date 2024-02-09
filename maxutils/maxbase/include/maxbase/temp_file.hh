/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <iostream>

namespace maxbase
{

/**
 * @brief class TempFile Names, touches and deletes a
 *        file by name in the destructor.
 *        This is not a ::tmpfile(), i.e., a crash might leave
 *        the file. TempDirectory can be used to manage
 *        a set of temp files.
 */
class TempFile final
{
public:
    /**
     * @brief TempFile Creates a read-write, user owened uniquely
     *              named file in the indicated directory.
     * @param dir Directory where to create the file.
     */
    TempFile(const std::string& dir = "/tmp");
    TempFile(TempFile&& rhs);
    ~TempFile();

    template<typename T>
    T make_stream(std::ios_base::openmode mode) const
    {
        return T(name(), mode);
    }

    // file name
    std::string name() const;

private:
    std::string m_name;
};

/**
 * @brief class TempDirectory Manages a directory for TempFile usage
 *        basically by deleting and creating it in the constructor,
 *        and deleting it in destructor.
 */
class TempDirectory final
{
public:
    /**
     * @brief TempDirectory Manage the given directory of temp files.
     *                      The directory is deleted and re-created
     *                      by the constructor, and again deleted
     *                      in the destructor.
     * @param dir - Must start with "/tmp/[char]" to avoid
     *              unnecessary catastrophe.
     */
    TempDirectory(const std::string& dir);

    // The passed in directory name was not valid or
    // the directory could not be created.
    bool is_valid() const;

    // Create a new TempFile in the managed directory.
    TempFile temp_file() const;

    // Delete the directory.
    ~TempDirectory();
private:
    std::string m_dir;
    bool        m_valid = true;
};
}

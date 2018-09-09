/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <string>

/**
 * TempFile is a class using which a temporary file can be created and
 * written to.
 */
class TempFile
{
    TempFile(const TempFile&);
    TempFile& operator=(const TempFile&);

public:
    /**
     * Constructor
     *
     * Create temporary file in /tmp
     */
    TempFile();

    /**
     * Destructor
     *
     * Close and unlink file.
     */
    ~TempFile();

    /**
     * The name of the created temporary file.
     *
     * @return The name of the file.
     */
    std::string name() const
    {
        return m_name;
    }

    /**
     * Write data to the file.
     *
     * @param pData  Pointer to data.
     * @param count  The number of bytes to write.
     */
    void write(const void* pData, size_t count);

    /**
     * Write data to the file.
     *
     * @param zData  Null terminated buffer to write.
     */
    void write(const char* zData);

private:
    int         m_fd;
    std::string m_name;
};
